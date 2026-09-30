package com.tinysynth

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

internal data class UiState(
    val midiName: String? = null,
    val soundFontName: String? = null,
    val positionMs: Long = 0L,
    val durationMs: Long = 0L,
    val playing: Boolean = false,
    val paused: Boolean = false,
    val hasMidi: Boolean = false,
    val hasSoundFont: Boolean = false,
    val busy: Boolean = false,
    val instruments: List<InstrumentChoice> = GeneralMidi.instruments,
    val selections: List<Int> = List(16) { 0 },
    val instrumentEpoch: Int = 0
) {
    val playEnabled: Boolean get() = hasMidi && hasSoundFont && !playing && !busy
    val pauseEnabled: Boolean get() = playing && !busy
    val stopEnabled: Boolean get() = (playing || paused) && !busy
    val seekEnabled: Boolean get() = hasMidi && durationMs > 0L && !busy
}

internal class SynthViewModel : ViewModel() {
    private val engine = MidiPlaybackEngine()
    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()
    private val _messages = MutableSharedFlow<String>(extraBufferCapacity = 4)
    val messages: SharedFlow<String> = _messages.asSharedFlow()
    private var busy = false

    init {
        engine.onError = { message -> _messages.tryEmit(message) }
        engine.onTransportChanged = { publish() }
        viewModelScope.launch {
            while (isActive) {
                publish()
                delay(80)
            }
        }
    }

    fun openMidi(context: Context, uri: Uri) {
        val app = context.applicationContext
        viewModelScope.launch {
            setBusy(true)
            try {
                val loaded = withContext(Dispatchers.IO) { readUri(app, uri) }
                SoundFontFiles.midiRejection(loaded.name, loaded.bytes)?.let { throw PlaybackException(it) }
                engine.loadMidi(loaded.name, loaded.bytes)
                publish()
            } catch (error: PlaybackException) {
                _messages.emit(error.message ?: "Could not open MIDI file.")
            } catch (_: OutOfMemoryError) {
                _messages.emit("That file is too large to load on this device.")
            } catch (error: Exception) {
                Log.e(TAG, "Open MIDI failed", error)
                _messages.emit("Could not open MIDI file.")
            } finally {
                setBusy(false)
            }
        }
    }

    fun openSoundFont(context: Context, uri: Uri) {
        val app = context.applicationContext
        viewModelScope.launch {
            setBusy(true)
            try {
                val loaded = withContext(Dispatchers.IO) { readUri(app, uri) }
                SoundFontFiles.soundFontRejection(loaded.name, loaded.bytes)?.let { throw PlaybackException(it) }
                engine.loadSoundFont(loaded.name, loaded.bytes)
                publish()
            } catch (error: PlaybackException) {
                _messages.emit(error.message ?: "Could not load SoundFont.")
            } catch (error: UnsatisfiedLinkError) {
                Log.e(TAG, "Synth library missing", error)
                _messages.emit("The synthesizer library failed to load.")
            } catch (_: OutOfMemoryError) {
                _messages.emit("That file is too large to load on this device.")
            } catch (error: Exception) {
                Log.e(TAG, "Open SoundFont failed", error)
                _messages.emit("Could not load SoundFont.")
            } finally {
                setBusy(false)
            }
        }
    }

    fun play() {
        try {
            engine.play()
            publish()
        } catch (error: PlaybackException) {
            _messages.tryEmit(error.message ?: "Playback failed.")
        } catch (error: Exception) {
            Log.e(TAG, "Play failed", error)
            _messages.tryEmit("Playback failed.")
        }
    }

    fun pause() {
        engine.pause()
        publish()
    }

    fun stop() {
        engine.stop()
        publish()
    }

    fun seek(positionMs: Long) {
        engine.seek(positionMs)
        publish()
    }

    fun setChannelInstrument(channel: Int, index: Int) {
        val choice = if (index <= 0) null else engine.instruments.getOrNull(index - 1)
        engine.setChannelOverride(channel, choice)
        publish()
    }

    override fun onCleared() {
        engine.close()
    }

    private fun setBusy(value: Boolean) {
        busy = value
        publish()
    }

    private fun publish() {
        val selections = List(16) { channel -> selectionIndex(engine.instruments, engine.overrideAt(channel)) }
        _state.update { previous ->
            val instrumentsChanged = previous.instruments != engine.instruments
            previous.copy(
                midiName = engine.midiName,
                soundFontName = engine.soundFontName,
                positionMs = engine.positionMs,
                durationMs = engine.durationMs,
                playing = engine.isPlaying,
                paused = engine.isPaused,
                hasMidi = engine.hasMidi,
                hasSoundFont = engine.hasSoundFont,
                busy = busy,
                instruments = engine.instruments,
                selections = selections,
                instrumentEpoch = if (instrumentsChanged) previous.instrumentEpoch + 1 else previous.instrumentEpoch
            )
        }
    }

    private fun readUri(context: Context, uri: Uri): NamedBytes {
        val name = displayName(context, uri)
        val bytes = context.contentResolver.openInputStream(uri)?.use { stream -> stream.readBytes() }
            ?: throw PlaybackException("Could not open the selected file.")
        return NamedBytes(name, bytes)
    }

    private fun displayName(context: Context, uri: Uri): String {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val name = cursor.getString(0)
                if (!name.isNullOrBlank()) {
                    return name
                }
            }
        }
        return uri.lastPathSegment ?: "file"
    }

    private data class NamedBytes(val name: String, val bytes: ByteArray)

    companion object {
        private const val TAG = "TinySynth"
    }
}
