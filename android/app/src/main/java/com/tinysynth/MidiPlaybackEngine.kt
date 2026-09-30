package com.tinysynth

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Handler
import android.os.Looper
import android.os.Process
import android.util.Log
import java.io.Closeable
import java.util.Locale

/**
 * SoundFont, MIDI file, AudioTrack output, and per-channel instrument overrides.
 * The UI talks only to this type. Synthesis runs on a dedicated thread; the
 * same lock guards the synth so seeks and program changes do not race a render.
 */
internal class MidiPlaybackEngine : Closeable {
    private val lock = java.lang.Object()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val overrides = arrayOfNulls<InstrumentChoice>(16)

    private var synth: SoundFontSynth? = null
    private var song: ParsedMidi? = null
    private var eventIndex = 0
    private var positionFrames = 0L
    private var playing = false
    private var paused = false
    private var released = false
    private var restart = false
    private var seekUs: Long? = null
    private var flush = false
    private var generation = 0

    private var audioTrack: AudioTrack? = null
    private var thread: Thread? = null

    var midiName: String? = null
        private set
    var soundFontName: String? = null
        private set
    var instruments: List<InstrumentChoice> = GeneralMidi.instruments
        private set

    var onError: ((String) -> Unit)? = null
    var onTransportChanged: (() -> Unit)? = null

    val hasMidi: Boolean get() = synchronized(lock) { song != null }
    val hasSoundFont: Boolean get() = synchronized(lock) { synth != null }
    val isPlaying: Boolean get() = synchronized(lock) { playing }
    val isPaused: Boolean get() = synchronized(lock) { paused }
    val durationMs: Long get() = synchronized(lock) { song?.durationMs ?: 0L }
    val positionMs: Long
        get() = synchronized(lock) { positionFrames * 1000L / SAMPLE_RATE }

    fun overrideAt(channel: Int): InstrumentChoice? = synchronized(lock) { overrides[channel] }

    fun loadMidi(name: String, data: ByteArray) {
        val parsed = SmfParser.parse(data)
        synchronized(lock) {
            song = parsed
            midiName = name
            positionFrames = 0L
            eventIndex = 0
            playing = false
            paused = false
            restart = false
            seekUs = null
            flush = true
            generation++
            synth?.reset()
        }
        signal()
        notifyTransport()
    }

    fun loadSoundFont(name: String, data: ByteArray) {
        val next = SoundFontSynth()
        try {
            next.load(data, SAMPLE_RATE)
        } catch (error: Throwable) {
            next.close()
            throw error
        }
        val resolved = next.presets()
            .sortedWith(compareBy<InstrumentChoice> { it.bank }.thenBy { it.program }.thenBy { it.name.lowercase(Locale.ROOT) })
            .ifEmpty { GeneralMidi.instruments }
        synchronized(lock) {
            val wasPlaying = playing && !paused
            val resumeFrames = positionFrames
            synth?.close()
            synth = next
            soundFontName = name
            instruments = resolved
            for (channel in 0 until 16) {
                if (overrides[channel] != null && selectionIndex(resolved, overrides[channel]) == 0) {
                    overrides[channel] = null
                }
            }
            if (wasPlaying && song != null) {
                positionFrames = resumeFrames
                restart = true
                flush = true
                generation++
            }
        }
        signal()
        notifyTransport()
    }

    fun play() {
        synchronized(lock) {
            if (synth == null) {
                throw PlaybackException("Select a SoundFont (.sf2) before playing.")
            }
            if (song == null) {
                throw PlaybackException("Open a MIDI file before playing.")
            }
            if (paused && playing) {
                paused = false
            } else if (!playing) {
                playing = true
                paused = false
                restart = true
                seekUs = null
                generation++
            }
        }
        ensureThread()
        signal()
        notifyTransport()
    }

    fun pause() {
        synchronized(lock) {
            if (!playing || paused) {
                return
            }
            paused = true
        }
        signal()
        notifyTransport()
    }

    fun stop() {
        synchronized(lock) {
            playing = false
            paused = false
            positionFrames = 0L
            eventIndex = 0
            restart = false
            seekUs = null
            flush = true
            generation++
            synth?.reset()
        }
        signal()
        notifyTransport()
    }

    fun seek(positionMs: Long) {
        synchronized(lock) {
            val current = song ?: return
            val targetUs = (positionMs.coerceAtLeast(0L) * 1000L).coerceAtMost(current.durationUs)
            positionFrames = targetUs * SAMPLE_RATE / 1_000_000L
            if (synth == null) {
                return
            }
            seekUs = targetUs
            generation++
        }
        signal()
        notifyTransport()
    }

    fun setChannelOverride(channel: Int, instrument: InstrumentChoice?) {
        require(channel in 0..15)
        synchronized(lock) {
            overrides[channel] = instrument
            val active = synth
            if (instrument != null && active != null && (playing || paused)) {
                if (!active.setBankProgram(channel, instrument.bank, instrument.program)) {
                    active.programChange(channel, instrument.program, drums = false)
                }
            }
        }
    }

    override fun close() {
        synchronized(lock) {
            if (released) {
                return
            }
            released = true
            playing = false
            lock.notifyAll()
        }
        thread?.join(2_000L)
        synchronized(lock) {
            synth?.close()
            synth = null
            val track = audioTrack
            audioTrack = null
            track?.release()
        }
    }

    private fun ensureThread() {
        val current = thread
        if (current != null && current.isAlive) {
            return
        }
        thread = Thread(::audioLoop, "TinySynthAudio").also { it.start() }
    }

    private fun signal() {
        synchronized(lock) {
            lock.notifyAll()
        }
    }

    private fun audioLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        val track = try {
            ensureTrack()
        } catch (error: PlaybackException) {
            reportError(error.message ?: "Could not open audio output.")
            return
        }
        val buffer = ShortArray(BLOCK_FRAMES * 2)
        while (true) {
            val action = synchronized(lock) {
                if (released) {
                    return@synchronized LoopAction.QUIT
                }
                val synthNow = synth
                val songNow = song
                val pendingSeek = seekUs
                if (pendingSeek != null && synthNow != null && songNow != null) {
                    arm(synthNow, songNow, pendingSeek)
                    seekUs = null
                    flush = true
                }
                if (restart && synthNow != null && songNow != null) {
                    val targetUs = positionFrames * 1_000_000L / SAMPLE_RATE
                    arm(synthNow, songNow, targetUs)
                    restart = false
                    flush = true
                }
                val shouldFlush = flush
                flush = false
                val mode = when {
                    playing && !paused && synthNow != null && songNow != null -> LoopMode.PLAY
                    else -> LoopMode.IDLE
                }
                LoopAction(mode, shouldFlush, generation)
            }
            if (action.mode == LoopMode.QUIT) {
                break
            }
            if (action.flush) {
                pauseAndFlush(track)
            }
            if (action.mode == LoopMode.IDLE) {
                if (track.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    track.pause()
                }
                var stopThread = false
                synchronized(lock) {
                    if (released) {
                        stopThread = true
                    } else if (!playing || paused || song == null || synth == null) {
                        try {
                            lock.wait(400L)
                        } catch (_: InterruptedException) {
                        }
                    }
                }
                if (stopThread) {
                    break
                }
                continue
            }
            val result = try {
                renderInto(buffer, BLOCK_FRAMES)
            } catch (error: Exception) {
                Log.e(TAG, "Render failed", error)
                synchronized(lock) {
                    playing = false
                    paused = false
                }
                reportError("Playback failed.")
                notifyTransport()
                continue
            }
            val stale = synchronized(lock) { generation != action.generation || !playing || paused }
            when (result) {
                RenderResult.WROTE -> if (!stale) {
                    if (track.playState != AudioTrack.PLAYSTATE_PLAYING) {
                        track.play()
                    }
                    writeAll(track, buffer, action.generation)
                }
                RenderResult.ENDED -> {
                    pauseAndFlush(track)
                    notifyTransport()
                }
                RenderResult.IDLE -> if (track.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    track.pause()
                }
            }
        }
    }

    private fun renderInto(buffer: ShortArray, frames: Int): RenderResult {
        synchronized(lock) {
            val synthNow = synth ?: return RenderResult.IDLE
            val songNow = song ?: return RenderResult.IDLE
            if (!playing || paused) {
                return RenderResult.IDLE
            }
            val events = songNow.events
            val songFrames = songNow.durationUs * SAMPLE_RATE / 1_000_000L
            var rendered = 0
            while (rendered < frames) {
                if (eventIndex >= events.size && positionFrames >= songFrames) {
                    finishSong(synthNow)
                    buffer.fill(0, rendered * 2, frames * 2)
                    return RenderResult.ENDED
                }
                val nextUs = if (eventIndex < events.size) events[eventIndex].timeUs else Long.MAX_VALUE
                val positionUs = positionFrames * 1_000_000L / SAMPLE_RATE
                if (nextUs <= positionUs) {
                    dispatch(synthNow, events[eventIndex])
                    eventIndex++
                    continue
                }
                val gapUs = if (nextUs == Long.MAX_VALUE) songNow.durationUs - positionUs else nextUs - positionUs
                val framesUntil = (gapUs.coerceAtLeast(0L) * SAMPLE_RATE / 1_000_000L).toInt()
                if (framesUntil <= 0) {
                    if (nextUs == Long.MAX_VALUE) {
                        finishSong(synthNow)
                        buffer.fill(0, rendered * 2, frames * 2)
                        return RenderResult.ENDED
                    }
                    positionFrames = nextUs * SAMPLE_RATE / 1_000_000L
                    if (positionFrames * 1_000_000L / SAMPLE_RATE < nextUs) {
                        positionFrames++
                    }
                    continue
                }
                val slice = minOf(frames - rendered, framesUntil)
                synthNow.render(buffer, rendered * 2, slice)
                positionFrames += slice
                rendered += slice
            }
            return RenderResult.WROTE
        }
    }

    private fun finishSong(synthNow: SoundFontSynth) {
        playing = false
        paused = false
        positionFrames = 0L
        eventIndex = 0
        restart = false
        synthNow.reset()
        flush = true
    }

    private fun arm(synthNow: SoundFontSynth, songNow: ParsedMidi, targetUs: Long) {
        val clamped = targetUs.coerceIn(0L, songNow.durationUs)
        applySnapshots(synthNow, SeekState.capture(songNow.events, clamped, overrides))
        positionFrames = clamped * SAMPLE_RATE / 1_000_000L
        eventIndex = 0
        val events = songNow.events
        while (eventIndex < events.size && events[eventIndex].timeUs <= clamped) {
            eventIndex++
        }
    }

    private fun applySnapshots(synthNow: SoundFontSynth, snapshots: List<ChannelSnapshot>) {
        synthNow.reset()
        for (channel in snapshots.indices) {
            val snap = snapshots[channel]
            val exactBank = snap.exactBank
            if (exactBank != null) {
                if (!synthNow.setBankProgram(channel, exactBank, snap.program)) {
                    synthNow.programChange(channel, snap.program, drums = false)
                }
            } else {
                if (snap.bankMsb != null) {
                    synthNow.control(channel, 0, snap.bankMsb)
                }
                if (snap.bankLsb != null) {
                    synthNow.control(channel, 32, snap.bankLsb)
                }
                synthNow.programChange(channel, snap.program, drums = channel == 9)
            }
            synthNow.control(channel, 7, snap.volume)
            synthNow.control(channel, 11, snap.expression)
            synthNow.control(channel, 10, snap.pan)
            synthNow.pitchBend(channel, snap.pitchBend)
            synthNow.setPitchRange(channel, snap.pitchRange)
            if (snap.sustain) {
                synthNow.control(channel, 64, 127)
            }
            for (note in snap.notes) {
                synthNow.noteOn(channel, note.key, note.velocity / 127f)
            }
        }
    }

    private fun dispatch(synthNow: SoundFontSynth, event: MidiEvent) {
        val channel = event.channel
        if (channel !in 0..15) {
            return
        }
        val override = overrides[channel]
        when (event.kind) {
            MidiKind.NOTE_ON -> {
                if (event.data1 in 0..127 && event.data2 > 0) {
                    synthNow.noteOn(channel, event.data1, event.data2 / 127f)
                }
            }
            MidiKind.NOTE_OFF -> if (event.data1 in 0..127) {
                synthNow.noteOff(channel, event.data1)
            }
            MidiKind.CC -> {
                if (override != null && (event.data1 == 0 || event.data1 == 32)) {
                    return
                }
                synthNow.control(channel, event.data1, event.data2)
            }
            MidiKind.PROGRAM -> {
                if (override != null) {
                    if (!synthNow.setBankProgram(channel, override.bank, override.program)) {
                        synthNow.programChange(channel, override.program, drums = false)
                    }
                } else {
                    synthNow.programChange(channel, event.data1 and 0x7F, drums = channel == 9)
                }
            }
            MidiKind.BEND -> {
                val value = ((event.data2 and 0x7F) shl 7) or (event.data1 and 0x7F)
                synthNow.pitchBend(channel, value)
            }
            MidiKind.TEMPO -> Unit
        }
    }

    private fun ensureTrack(): AudioTrack {
        audioTrack?.let { return it }
        val minBytes = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT
        )
        if (minBytes <= 0) {
            throw PlaybackException("This device cannot play 44.1 kHz stereo audio.")
        }
        val bufferBytes = maxOf(minBytes, SAMPLE_RATE * 2 * 2 * 120 / 1000)
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(bufferBytes)
            .build()
        if (track.state != AudioTrack.STATE_INITIALIZED) {
            track.release()
            throw PlaybackException("Could not open audio output.")
        }
        audioTrack = track
        return track
    }

    private fun writeAll(track: AudioTrack, buffer: ShortArray, generationAtStart: Int) {
        var offset = 0
        while (offset < buffer.size) {
            val stop = synchronized(lock) {
                released || !playing || paused || generation != generationAtStart
            }
            if (stop) {
                return
            }
            val wrote = track.write(buffer, offset, buffer.size - offset, AudioTrack.WRITE_NON_BLOCKING)
            if (wrote < 0) {
                return
            }
            if (wrote == 0) {
                try {
                    Thread.sleep(4L)
                } catch (_: InterruptedException) {
                    return
                }
                continue
            }
            offset += wrote
        }
    }

    private fun pauseAndFlush(track: AudioTrack) {
        track.pause()
        track.flush()
    }

    private fun notifyTransport() {
        mainHandler.post { onTransportChanged?.invoke() }
    }

    private fun reportError(message: String) {
        mainHandler.post { onError?.invoke(message) }
    }

    private enum class LoopMode { PLAY, IDLE, QUIT }

    private data class LoopAction(val mode: LoopMode, val flush: Boolean, val generation: Int) {
        companion object {
            val QUIT = LoopAction(LoopMode.QUIT, flush = false, generation = 0)
        }
    }

    private enum class RenderResult { WROTE, ENDED, IDLE }

    companion object {
        const val SAMPLE_RATE = 44100
        private const val BLOCK_FRAMES = 256
        private const val TAG = "TinySynth"
    }
}
