package com.tinysynth

/**
 * A selectable instrument: a SoundFont preset, or a General MIDI name used
 * before a font is loaded. Bank and program are the values sent to the synth.
 */
internal data class InstrumentChoice(
    val bank: Int,
    val program: Int,
    val name: String
) {
    val displayName: String
        get() = if (bank == 0) {
            "%03d  %s".format(program + 1, name)
        } else {
            "%03d:%03d  %s".format(bank, program, name)
        }
}

/** Spinner index: 0 is "(from MIDI file)", then [instruments] in order. */
internal fun selectionIndex(instruments: List<InstrumentChoice>, choice: InstrumentChoice?): Int {
    if (choice == null) {
        return 0
    }
    val index = instruments.indexOfFirst { it.bank == choice.bank && it.program == choice.program }
    return if (index < 0) 0 else index + 1
}
