package com.tinysynth

import java.util.Locale

/** Extension and RIFF checks shared by the UI and unit tests. */
internal object SoundFontFiles {
    const val DLS_MESSAGE = "DLS soundfonts are not supported. Please choose an SF2 file."
    const val SF2_MESSAGE = "Only SoundFont 2 (.sf2) files are supported."
    const val MIDI_MESSAGE = "Choose a .mid or .midi file."

    /** Null when [data] may be loaded as an SF2. */
    fun soundFontRejection(name: String, data: ByteArray): String? {
        val ext = extension(name)
        val riff = riffType(data)
        if (ext == "dls" || riff == "DLS ") {
            return DLS_MESSAGE
        }
        if (ext == "sf2" || riff == "sfbk") {
            return null
        }
        return SF2_MESSAGE
    }

    /** Null when [data] should be handed to the MIDI parser. */
    fun midiRejection(name: String, data: ByteArray): String? {
        val ext = extension(name)
        if (ext == "mid" || ext == "midi" || isMidiHeader(data)) {
            return null
        }
        return MIDI_MESSAGE
    }

    private fun extension(name: String): String =
        name.substringAfterLast('.', "").lowercase(Locale.ROOT)

    private fun isMidiHeader(data: ByteArray): Boolean =
        data.size >= 4 && data.copyOfRange(0, 4).decodeToString() == "MThd"

    private fun riffType(data: ByteArray): String? {
        if (data.size < 12) {
            return null
        }
        if (data.copyOfRange(0, 4).decodeToString() != "RIFF") {
            return null
        }
        return data.copyOfRange(8, 12).decodeToString()
    }
}
