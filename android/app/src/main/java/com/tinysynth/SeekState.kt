package com.tinysynth

internal data class HeldNote(val key: Int, val velocity: Int)

/**
 * Channel state at a seek point: controllers, program, and notes that are
 * still held (including notes held by the sustain pedal). Rendering every
 * sample up to a far seek point is too slow on a phone, so playback chases
 * this snapshot and then continues from the next event.
 */
internal data class ChannelSnapshot(
    val program: Int,
    val bankMsb: Int?,
    val bankLsb: Int?,
    /** Set when a user override replaces the file's bank and program. */
    val exactBank: Int?,
    val volume: Int,
    val expression: Int,
    val pan: Int,
    val pitchBend: Int,
    val sustain: Boolean,
    val pitchRange: Float,
    val notes: List<HeldNote>
)

internal object SeekState {
    fun capture(
        events: List<MidiEvent>,
        targetUs: Long,
        overrides: Array<InstrumentChoice?>
    ): List<ChannelSnapshot> {
        val channels = Array(16) { Channel() }
        for (event in events) {
            if (event.timeUs > targetUs) {
                break
            }
            if (event.channel !in 0..15) {
                continue
            }
            val channel = channels[event.channel]
            val override = overrides[event.channel]
            when (event.kind) {
                MidiKind.NOTE_ON -> channel.noteOn(event.data1, event.data2)
                MidiKind.NOTE_OFF -> channel.noteOff(event.data1)
                MidiKind.CC -> channel.control(event.data1, event.data2, override)
                MidiKind.PROGRAM -> if (override == null) {
                    channel.program = event.data1 and 0x7F
                }
                MidiKind.BEND -> {
                    channel.pitchBend = ((event.data2 and 0x7F) shl 7) or (event.data1 and 0x7F)
                }
                MidiKind.TEMPO -> Unit
            }
        }
        return List(16) { index -> channels[index].snapshot(overrides[index]) }
    }

    private class Channel {
        var program: Int = 0
        var bankMsb: Int? = null
        var bankLsb: Int? = null
        var volume: Int = 127
        var expression: Int = 127
        var pan: Int = 64
        var pitchBend: Int = 8192
        var sustain: Boolean = false
        var pitchRange: Float = 2f
        private var rpn: Int = 0xFFFF
        private var rpnData: Int = 0
        private val held = IntArray(128) { -1 }
        private val sustained = BooleanArray(128)
        private val sustainVelocity = IntArray(128)

        fun noteOn(key: Int, velocity: Int) {
            if (key !in 0..127 || velocity <= 0) {
                return
            }
            held[key] = velocity
            sustained[key] = false
        }

        fun noteOff(key: Int) {
            if (key !in 0..127 || held[key] < 0) {
                return
            }
            if (sustain) {
                sustained[key] = true
                sustainVelocity[key] = held[key]
            }
            held[key] = -1
        }

        fun control(controller: Int, value: Int, override: InstrumentChoice?) {
            val clipped = value and 0x7F
            when (controller) {
                0 -> if (override == null) bankMsb = clipped
                32 -> if (override == null) bankLsb = clipped
                7 -> volume = clipped
                11 -> expression = clipped
                10 -> pan = clipped
                64 -> {
                    val down = clipped >= 64
                    if (sustain && !down) {
                        sustained.fill(false)
                    }
                    sustain = down
                }
                120, 123 -> clearNotes()
                121 -> resetControllers(override)
                6, 38, 100, 101 -> dataEntry(controller, clipped)
            }
        }

        fun snapshot(override: InstrumentChoice?): ChannelSnapshot {
            val notes = ArrayList<HeldNote>()
            for (key in 0..127) {
                val velocity = when {
                    held[key] >= 0 -> held[key]
                    sustained[key] -> sustainVelocity[key]
                    else -> -1
                }
                if (velocity > 0) {
                    notes.add(HeldNote(key, velocity))
                }
            }
            return if (override != null) {
                ChannelSnapshot(
                    program = override.program,
                    bankMsb = null,
                    bankLsb = null,
                    exactBank = override.bank,
                    volume = volume,
                    expression = expression,
                    pan = pan,
                    pitchBend = pitchBend,
                    sustain = sustain,
                    pitchRange = pitchRange,
                    notes = notes
                )
            } else {
                ChannelSnapshot(
                    program = program,
                    bankMsb = bankMsb,
                    bankLsb = bankLsb,
                    exactBank = null,
                    volume = volume,
                    expression = expression,
                    pan = pan,
                    pitchBend = pitchBend,
                    sustain = sustain,
                    pitchRange = pitchRange,
                    notes = notes
                )
            }
        }

        private fun clearNotes() {
            held.fill(-1)
            sustained.fill(false)
        }

        private fun resetControllers(override: InstrumentChoice?) {
            volume = 127
            expression = 127
            pan = 64
            sustain = false
            pitchRange = 2f
            pitchBend = 8192
            rpn = 0xFFFF
            rpnData = 0
            sustained.fill(false)
            if (override == null) {
                bankMsb = null
                bankLsb = null
            }
        }

        /** Pitch-bend range (RPN 0), matching TinySoundFont's data-entry handling. */
        private fun dataEntry(controller: Int, value: Int) {
            when (controller) {
                101 -> rpn = ((if (rpn == 0xFFFF) 0 else rpn) and 0x7F) or (value shl 7)
                100 -> rpn = ((if (rpn == 0xFFFF) 0 else rpn) and 0x3F80) or value
                6 -> {
                    rpnData = (rpnData and 0x7F) or (value shl 7)
                    applyPitchRange()
                }
                38 -> {
                    rpnData = (rpnData and 0x3F80) or value
                    applyPitchRange()
                }
            }
        }

        private fun applyPitchRange() {
            if (rpn == 0) {
                pitchRange = (rpnData shr 7) + 0.01f * (rpnData and 0x7F)
            }
        }
    }
}
