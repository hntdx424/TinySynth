package com.tinysynth

import androidx.annotation.Keep

/**
 * One loaded SoundFont. Native calls are serialized by [MidiPlaybackEngine].
 */
@Keep
internal class SoundFontSynth {
    private var handle: Long = 0L

    fun load(data: ByteArray, sampleRate: Int) {
        check(handle == 0L) { "SoundFont is already loaded" }
        val loaded = nativeLoad(data)
        if (loaded == 0L) {
            throw PlaybackException("Could not load SoundFont. The file may be damaged or is not a valid SF2.")
        }
        handle = loaded
        nativeConfigure(loaded, sampleRate, GAIN_DB)
    }

    fun close() {
        val current = handle
        handle = 0L
        if (current != 0L) {
            nativeClose(current)
        }
    }

    fun reset() {
        val current = handle
        if (current != 0L) {
            nativeReset(current)
        }
    }

    fun presets(): List<InstrumentChoice> {
        val current = handle
        if (current == 0L) {
            return emptyList()
        }
        val count = nativePresetCount(current)
        if (count <= 0) {
            return emptyList()
        }
        val pairs = nativePresetBankProgram(current) ?: return emptyList()
        val names = nativePresetNames(current) ?: return emptyList()
        val list = ArrayList<InstrumentChoice>(count)
        for (index in 0 until count) {
            val bank = pairs.getOrElse(index * 2) { 0 }
            val program = pairs.getOrElse(index * 2 + 1) { 0 }
            val name = names.getOrElse(index) { "" }.ifBlank { "Preset $program" }
            list.add(InstrumentChoice(bank, program, name))
        }
        return list
    }

    fun noteOn(channel: Int, key: Int, velocity: Float) {
        val current = handle
        if (current != 0L) {
            nativeNoteOn(current, channel, key, velocity)
        }
    }

    fun noteOff(channel: Int, key: Int) {
        val current = handle
        if (current != 0L) {
            nativeNoteOff(current, channel, key)
        }
    }

    fun control(channel: Int, controller: Int, value: Int) {
        val current = handle
        if (current != 0L) {
            nativeControl(current, channel, controller, value)
        }
    }

    fun programChange(channel: Int, program: Int, drums: Boolean) {
        val current = handle
        if (current != 0L) {
            nativeProgram(current, channel, program, drums)
        }
    }

    /** @return false when the SoundFont has no preset at this bank and program. */
    fun setBankProgram(channel: Int, bank: Int, program: Int): Boolean {
        val current = handle
        if (current == 0L) {
            return false
        }
        return nativeBankProgram(current, channel, bank, program)
    }

    fun pitchBend(channel: Int, value: Int) {
        val current = handle
        if (current != 0L) {
            nativePitchBend(current, channel, value)
        }
    }

    fun setPitchRange(channel: Int, semitones: Float) {
        val current = handle
        if (current != 0L) {
            nativePitchRange(current, channel, semitones)
        }
    }

    fun render(buffer: ShortArray, offsetShorts: Int, frames: Int) {
        val current = handle
        if (current != 0L && frames > 0) {
            nativeRender(current, buffer, offsetShorts, frames)
        }
    }

    private external fun nativeLoad(data: ByteArray): Long
    private external fun nativeConfigure(handle: Long, sampleRate: Int, gainDb: Float)
    private external fun nativeClose(handle: Long)
    private external fun nativeReset(handle: Long)
    private external fun nativePresetCount(handle: Long): Int
    private external fun nativePresetBankProgram(handle: Long): IntArray?
    private external fun nativePresetNames(handle: Long): Array<String>?
    private external fun nativeNoteOn(handle: Long, channel: Int, key: Int, velocity: Float)
    private external fun nativeNoteOff(handle: Long, channel: Int, key: Int)
    private external fun nativeControl(handle: Long, channel: Int, controller: Int, value: Int)
    private external fun nativeProgram(handle: Long, channel: Int, program: Int, drums: Boolean)
    private external fun nativeBankProgram(handle: Long, channel: Int, bank: Int, program: Int): Boolean
    private external fun nativePitchBend(handle: Long, channel: Int, value: Int)
    private external fun nativePitchRange(handle: Long, channel: Int, semitones: Float)
    private external fun nativeRender(handle: Long, buffer: ShortArray, offsetShorts: Int, frames: Int)

    companion object {
        /** A few dB of headroom so dense MIDI does not clip the 16-bit stream. */
        private const val GAIN_DB = -8f

        init {
            System.loadLibrary("tinysynth")
        }
    }
}
