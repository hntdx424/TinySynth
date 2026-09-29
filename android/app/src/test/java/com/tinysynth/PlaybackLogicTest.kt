package com.tinysynth

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayOutputStream

class PlaybackLogicTest {
    @Test
    fun generalMidiNamesMatchTheWindowsPlayer() {
        assertEquals(128, GeneralMidi.instruments.size)
        assertEquals(0, GeneralMidi.instruments[0].bank)
        assertEquals(0, GeneralMidi.instruments[0].program)
        assertEquals("001  Acoustic Grand Piano", GeneralMidi.instruments[0].displayName)
        assertEquals("128  Gunshot", GeneralMidi.instruments[127].displayName)
        assertEquals("128:000  Standard Kit", InstrumentChoice(128, 0, "Standard Kit").displayName)
        assertEquals(1, selectionIndex(GeneralMidi.instruments, GeneralMidi.instruments[0]))
        assertEquals(0, selectionIndex(GeneralMidi.instruments, null))
        assertEquals(0, selectionIndex(GeneralMidi.instruments, InstrumentChoice(128, 0, "Kit")))
    }

    @Test
    fun rejectsDlsAndAcceptsSf2() {
        assertEquals(SoundFontFiles.DLS_MESSAGE, SoundFontFiles.soundFontRejection("kit.dls", byteArrayOf()))
        assertEquals(SoundFontFiles.DLS_MESSAGE, SoundFontFiles.soundFontRejection("kit.bin", riff("DLS ")))
        assertNull(SoundFontFiles.soundFontRejection("piano.sf2", byteArrayOf(1)))
        assertNull(SoundFontFiles.soundFontRejection("font.bin", riff("sfbk")))
        assertEquals(SoundFontFiles.SF2_MESSAGE, SoundFontFiles.soundFontRejection("piano.wav", byteArrayOf(1, 2, 3)))
        assertNull(SoundFontFiles.midiRejection("song.mid", byteArrayOf()))
        assertNull(SoundFontFiles.midiRejection("song.bin", "MThd".encodeToByteArray()))
        assertEquals(SoundFontFiles.MIDI_MESSAGE, SoundFontFiles.midiRejection("song.wav", byteArrayOf(1)))
    }

    @Test
    fun parsesQuarterNoteAtDefaultTempo() {
        val midi = format0(
            ppq = 480,
            track(
                event(0, 0x90, 60, 64),
                event(480, 0x80, 60, 0)
            )
        )
        val parsed = SmfParser.parse(midi)
        assertEquals(500_000L, parsed.durationUs)
        assertEquals(2, parsed.events.size)
        assertEquals(MidiKind.NOTE_ON, parsed.events[0].kind)
        assertEquals(0L, parsed.events[0].timeUs)
        assertEquals(60, parsed.events[0].data1)
        assertEquals(64, parsed.events[0].data2)
        assertEquals(MidiKind.NOTE_OFF, parsed.events[1].kind)
        assertEquals(500_000L, parsed.events[1].timeUs)
    }

    @Test
    fun appliesTempoChangeAndRunningStatus() {
        val track = track(
            bytes(0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20),
            event(0, 0x90, 60, 64),
            bytes(0x00, 64, 80),
            bytes(0x83, 0x60, 60, 0),
            bytes(0, 0xFF, 0x51, 3, 0x03, 0xD0, 0x90),
            event(480, 0x90, 62, 40)
        )
        val parsed = SmfParser.parse(format0(ppq = 480, track))
        val noteOns = parsed.events.filter { it.kind == MidiKind.NOTE_ON }
        assertEquals(3, noteOns.size)
        assertEquals(64, noteOns[1].data1)
        assertEquals(80, noteOns[1].data2)
        assertEquals(0L, noteOns[1].timeUs)
        assertEquals(MidiKind.NOTE_OFF, parsed.events[2].kind)
        assertEquals(500_000L, parsed.events[2].timeUs)
        assertEquals(750_000L, noteOns[2].timeUs)
        assertEquals(750_000L, parsed.durationUs)
    }

    @Test
    fun mergesFormat1TracksInTrackOrder() {
        val tempo = track(bytes(0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20))
        val music = track(event(0, 0xC0, 5), event(0, 0x90, 60, 40))
        val parsed = SmfParser.parse(smf(format = 1, ppq = 480, tempo, music))
        assertEquals(MidiKind.PROGRAM, parsed.events[0].kind)
        assertEquals(5, parsed.events[0].data1)
        assertEquals(MidiKind.NOTE_ON, parsed.events[1].kind)
        assertEquals(0L, parsed.events[0].timeUs)
        assertEquals(0L, parsed.events[1].timeUs)
    }

    @Test
    fun rejectsUnsupportedOrTruncatedMidi() {
        assertThrowsMessage("format 2") {
            SmfParser.parse(smf(2, 480, track(event(0, 0x90, 60, 40))))
        }
        assertThrowsMessage("not a Standard MIDI file") { SmfParser.parse("HELLO".encodeToByteArray()) }
        assertThrowsMessage("truncated") { SmfParser.parse("MThd".encodeToByteArray()) }
    }

    @Test
    fun chaseKeepsHeldNotesAndAppliesOverrides() {
        val events = listOf(
            MidiEvent(0, MidiKind.CC, 0, 0, 1),
            MidiEvent(0, MidiKind.CC, 0, 32, 2),
            MidiEvent(0, MidiKind.PROGRAM, 0, 5, 0),
            MidiEvent(0, MidiKind.NOTE_ON, 0, 60, 100),
            MidiEvent(1_000, MidiKind.NOTE_OFF, 0, 60, 0),
            MidiEvent(2_000, MidiKind.NOTE_ON, 0, 64, 80),
            MidiEvent(2_000, MidiKind.BEND, 0, 0, 64)
        )
        val early = SeekState.capture(events, 1_500, arrayOfNulls(16))
        assertEquals(5, early[0].program)
        assertEquals(1, early[0].bankMsb)
        assertEquals(2, early[0].bankLsb)
        assertNull(early[0].exactBank)
        assertTrue(early[0].notes.isEmpty())

        val later = SeekState.capture(events, 2_000, arrayOfNulls(16))
        assertEquals(listOf(HeldNote(64, 80)), later[0].notes)
        assertEquals(8192, later[0].pitchBend)

        val override = arrayOfNulls<InstrumentChoice>(16)
        override[0] = InstrumentChoice(128, 4, "Kit")
        val replaced = SeekState.capture(events, 2_000, override)
        assertEquals(128, replaced[0].exactBank)
        assertEquals(4, replaced[0].program)
        assertNull(replaced[0].bankMsb)
    }

    @Test
    fun sustainPedalHoldsAndReleasesNotes() {
        val events = listOf(
            MidiEvent(0, MidiKind.NOTE_ON, 1, 60, 90),
            MidiEvent(10, MidiKind.CC, 1, 64, 127),
            MidiEvent(20, MidiKind.NOTE_OFF, 1, 60, 0)
        )
        val held = SeekState.capture(events, 30, arrayOfNulls(16))
        assertEquals(listOf(HeldNote(60, 90)), held[1].notes)
        assertTrue(held[1].sustain)

        val released = SeekState.capture(
            events + MidiEvent(40, MidiKind.CC, 1, 64, 0),
            50,
            arrayOfNulls(16)
        )
        assertTrue(released[1].notes.isEmpty())
        assertTrue(!released[1].sustain)
    }

    @Test
    fun allNotesOffClearsHeldNotes() {
        val events = listOf(
            MidiEvent(0, MidiKind.NOTE_ON, 2, 60, 70),
            MidiEvent(10, MidiKind.CC, 2, 123, 0)
        )
        val snapshot = SeekState.capture(events, 20, arrayOfNulls(16))
        assertTrue(snapshot[2].notes.isEmpty())
    }

    private fun assertThrowsMessage(fragment: String, block: () -> Unit) {
        try {
            block()
        } catch (error: PlaybackException) {
            assertTrue(error.message.orEmpty(), error.message.orEmpty().contains(fragment))
            return
        }
        throw AssertionError("Expected PlaybackException containing $fragment")
    }

    private fun riff(type: String): ByteArray {
        val data = ByteArray(12)
        "RIFF".encodeToByteArray().copyInto(data)
        type.encodeToByteArray().copyInto(data, destinationOffset = 8)
        return data
    }

    private fun format0(ppq: Int, track: ByteArray): ByteArray = smf(0, ppq, track)

    private fun smf(format: Int, ppq: Int, vararg tracks: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(byteArrayOf('M'.code.toByte(), 'T'.code.toByte(), 'h'.code.toByte(), 'd'.code.toByte()))
        out.write(intBytes(6))
        out.write(shortBytes(format))
        out.write(shortBytes(tracks.size))
        out.write(shortBytes(ppq))
        for (track in tracks) {
            out.write(byteArrayOf('M'.code.toByte(), 'T'.code.toByte(), 'r'.code.toByte(), 'k'.code.toByte()))
            out.write(intBytes(track.size))
            out.write(track)
        }
        return out.toByteArray()
    }

    private fun track(vararg events: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        for (event in events) {
            out.write(event)
        }
        out.write(byteArrayOf(0, 0xFF.toByte(), 0x2F, 0))
        return out.toByteArray()
    }

    private fun event(delta: Int, status: Int, data1: Int, data2: Int? = null): ByteArray {
        val body = ArrayList<Byte>()
        vlq(delta).forEach { body.add(it) }
        body.add(status.toByte())
        body.add(data1.toByte())
        if (data2 != null && (status and 0xF0) != 0xC0 && (status and 0xF0) != 0xD0) {
            body.add(data2.toByte())
        }
        return body.toByteArray()
    }

    private fun bytes(vararg values: Int): ByteArray = values.map { it.toByte() }.toByteArray()

    private fun vlq(value: Int): ByteArray {
        require(value >= 0)
        if (value < 0x80) {
            return byteArrayOf(value.toByte())
        }
        val bytes = ArrayList<Int>()
        var remaining = value
        bytes.add(remaining and 0x7F)
        remaining = remaining shr 7
        while (remaining > 0) {
            bytes.add((remaining and 0x7F) or 0x80)
            remaining = remaining shr 7
        }
        bytes.reverse()
        return bytes.map { it.toByte() }.toByteArray()
    }

    private fun shortBytes(value: Int): ByteArray =
        byteArrayOf((value shr 8).toByte(), value.toByte())

    private fun intBytes(value: Int): ByteArray = byteArrayOf(
        (value shr 24).toByte(),
        (value shr 16).toByte(),
        (value shr 8).toByte(),
        value.toByte()
    )
}
