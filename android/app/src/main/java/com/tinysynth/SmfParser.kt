package com.tinysynth

internal enum class MidiKind {
    NOTE_OFF,
    NOTE_ON,
    CC,
    PROGRAM,
    BEND,
    TEMPO
}

internal data class MidiEvent(
    val timeUs: Long,
    val kind: MidiKind,
    val channel: Int,
    val data1: Int,
    val data2: Int
)

internal class ParsedMidi(
    val events: List<MidiEvent>,
    val durationUs: Long
) {
    val durationMs: Long get() = durationUs / 1000L
}

/**
 * Reads Standard MIDI Files (format 0 and 1) into a single time-ordered
 * event list. Tempo and SMPTE division are both applied.
 */
internal object SmfParser {
    fun parse(data: ByteArray): ParsedMidi {
        if (data.size < 4 || !startsWith(data, "MThd")) {
            throw PlaybackException("This file is not a Standard MIDI file.")
        }
        if (data.size < 14) {
            throw PlaybackException("This MIDI file is truncated.")
        }
        val reader = Reader(data)
        reader.ascii(4)
        val headerLength = reader.u32()
        if (headerLength < 6) {
            throw PlaybackException("This MIDI file has a truncated header.")
        }
        val format = reader.u16()
        val trackCount = reader.u16()
        val division = reader.u16()
        if (headerLength > 6) {
            reader.skip((headerLength - 6).toInt())
        }
        if (format != 0 && format != 1) {
            throw PlaybackException("MIDI format $format is not supported. Use a format 0 or 1 file.")
        }
        if (trackCount <= 0) {
            throw PlaybackException("This MIDI file has no tracks.")
        }

        val smpte = division and 0x8000 != 0
        val ppq = if (smpte) 0 else division
        val ticksPerSecond = if (smpte) smpteTicksPerSecond(division) else 0
        if (!smpte && ppq <= 0) {
            throw PlaybackException("This MIDI file has an unsupported time division.")
        }

        val raw = ArrayList<RawEvent>(256)
        val trackEndTick = LongArray(trackCount)
        var order = 0
        for (track in 0 until trackCount) {
            if (reader.remaining() < 8) {
                throw PlaybackException("This MIDI file is truncated.")
            }
            val tag = reader.ascii(4)
            val length = reader.u32()
            if (length > Int.MAX_VALUE || reader.position > reader.limit - length) {
                throw PlaybackException("This MIDI file is truncated.")
            }
            if (tag != "MTrk") {
                reader.skip(length.toInt())
                continue
            }
            val trackEnd = reader.position + length.toInt()
            val savedLimit = reader.limit
            reader.limit = trackEnd
            var tick = 0L
            var running = 0
            while (reader.position < trackEnd) {
                tick += reader.vlq()
                if (reader.position >= trackEnd) {
                    break
                }
                val statusByte = reader.u8()
                if (statusByte < 0x80) {
                    if (running == 0) {
                        throw PlaybackException("This MIDI file has a corrupt event.")
                    }
                    reader.rewindOne()
                    order = readChannel(reader, raw, tick, track, order, running)
                } else if (statusByte in 0x80..0xEF) {
                    running = statusByte
                    order = readChannel(reader, raw, tick, track, order, statusByte)
                } else if (statusByte == 0xFF) {
                    order = readMeta(reader, raw, tick, track, order)
                    if (raw.lastOrNull()?.endOfTrack == true) {
                        raw.removeAt(raw.lastIndex)
                        break
                    }
                } else if (statusByte == 0xF0 || statusByte == 0xF7) {
                    running = 0
                    val size = reader.vlq()
                    if (size > Int.MAX_VALUE) {
                        throw PlaybackException("This MIDI file is truncated.")
                    }
                    reader.skip(size.toInt())
                } else if (statusByte == 0xF1 || statusByte == 0xF3) {
                    reader.u8()
                } else if (statusByte == 0xF2) {
                    reader.u8()
                    reader.u8()
                }
            }
            reader.limit = savedLimit
            reader.position = trackEnd
            trackEndTick[track] = tick
        }

        raw.sortWith(compareBy({ it.tick }, { it.track }, { it.order }))
        val tempos = tempoMap(raw, smpte)
        var maxTick = trackEndTick.maxOrNull() ?: 0L
        for (event in raw) {
            if (event.tick > maxTick) {
                maxTick = event.tick
            }
        }

        val events = ArrayList<MidiEvent>(raw.size)
        for (event in raw) {
            if (event.kind == MidiKind.TEMPO) {
                continue
            }
            events.add(
                MidiEvent(
                    timeUs = tickToUs(event.tick, smpte, ticksPerSecond, ppq, tempos),
                    kind = event.kind,
                    channel = event.channel,
                    data1 = event.data1,
                    data2 = event.data2
                )
            )
        }
        val durationUs = tickToUs(maxTick, smpte, ticksPerSecond, ppq, tempos)
        return ParsedMidi(events, durationUs)
    }

    private fun readChannel(
        reader: Reader,
        raw: MutableList<RawEvent>,
        tick: Long,
        track: Int,
        order: Int,
        status: Int
    ): Int {
        val command = status and 0xF0
        val channel = status and 0x0F
        val data1 = reader.u8()
        val data2 = if (command == 0xC0 || command == 0xD0) 0 else reader.u8()
        val kind = when (command) {
            0x80 -> MidiKind.NOTE_OFF
            0x90 -> if (data2 == 0) MidiKind.NOTE_OFF else MidiKind.NOTE_ON
            0xB0 -> MidiKind.CC
            0xC0 -> MidiKind.PROGRAM
            0xE0 -> MidiKind.BEND
            else -> null
        }
        if (kind != null) {
            raw.add(RawEvent(tick, track, order, kind, channel, data1, data2, 0, false))
        }
        return order + 1
    }

    private fun readMeta(
        reader: Reader,
        raw: MutableList<RawEvent>,
        tick: Long,
        track: Int,
        order: Int
    ): Int {
        val type = reader.u8()
        val size = reader.vlq()
        if (size > Int.MAX_VALUE) {
            throw PlaybackException("This MIDI file is truncated.")
        }
        if (type == 0x2F) {
            reader.skip(size.toInt())
            raw.add(RawEvent(tick, track, order, MidiKind.TEMPO, 0, 0, 0, 0, true))
            return order + 1
        }
        if (type == 0x51 && size >= 3) {
            val tempo = (reader.u8() shl 16) or (reader.u8() shl 8) or reader.u8()
            if (size > 3) {
                reader.skip((size - 3).toInt())
            }
            if (tempo > 0) {
                raw.add(RawEvent(tick, track, order, MidiKind.TEMPO, 0, 0, 0, tempo, false))
            }
            return order + 1
        }
        reader.skip(size.toInt())
        return order + 1
    }

    private fun tempoMap(raw: List<RawEvent>, smpte: Boolean): List<TempoPoint> {
        if (smpte) {
            return emptyList()
        }
        val points = ArrayList<TempoPoint>()
        points.add(TempoPoint(0L, 500_000))
        for (event in raw) {
            if (event.kind != MidiKind.TEMPO || event.tempoUs <= 0) {
                continue
            }
            if (event.tick == 0L || points.last().tick == event.tick) {
                points[points.lastIndex] = TempoPoint(event.tick, event.tempoUs)
            } else {
                points.add(TempoPoint(event.tick, event.tempoUs))
            }
        }
        return points
    }

    private fun tickToUs(
        tick: Long,
        smpte: Boolean,
        ticksPerSecond: Int,
        ppq: Int,
        tempos: List<TempoPoint>
    ): Long {
        if (smpte) {
            return tick * 1_000_000L / ticksPerSecond
        }
        var lastTick = 0L
        var lastUs = 0L
        var microsPerQuarter = tempos.first().microsPerQuarter
        var index = 0
        while (index + 1 < tempos.size && tempos[index + 1].tick <= tick) {
            val next = tempos[index + 1]
            lastUs += (next.tick - lastTick) * microsPerQuarter / ppq
            lastTick = next.tick
            index++
            microsPerQuarter = tempos[index].microsPerQuarter
        }
        return lastUs + (tick - lastTick) * microsPerQuarter / ppq
    }

    private fun startsWith(data: ByteArray, text: String): Boolean {
        val bytes = text.encodeToByteArray()
        if (data.size < bytes.size) {
            return false
        }
        for (index in bytes.indices) {
            if (data[index] != bytes[index]) {
                return false
            }
        }
        return true
    }

    private fun smpteTicksPerSecond(division: Int): Int {
        val fpsByte = (division shr 8) and 0xFF
        val framesPerSecond = 256 - fpsByte
        val ticksPerFrame = division and 0xFF
        if (framesPerSecond <= 0 || ticksPerFrame <= 0) {
            throw PlaybackException("This MIDI file has an unsupported time division.")
        }
        return framesPerSecond * ticksPerFrame
    }

    private data class TempoPoint(val tick: Long, val microsPerQuarter: Int)

    private data class RawEvent(
        val tick: Long,
        val track: Int,
        val order: Int,
        val kind: MidiKind,
        val channel: Int,
        val data1: Int,
        val data2: Int,
        val tempoUs: Int,
        val endOfTrack: Boolean
    )

    private class Reader(private val data: ByteArray) {
        var position: Int = 0
        var limit: Int = data.size

        fun remaining(): Int = limit - position

        fun u8(): Int {
            if (position >= limit) {
                throw PlaybackException("This MIDI file is truncated.")
            }
            return data[position++].toInt() and 0xFF
        }

        fun u16(): Int = (u8() shl 8) or u8()

        fun u32(): Long {
            val hi = u8().toLong()
            return (hi shl 24) or (u8().toLong() shl 16) or (u8().toLong() shl 8) or u8().toLong()
        }

        fun ascii(count: Int): String {
            val chars = CharArray(count)
            for (i in 0 until count) {
                chars[i] = u8().toChar()
            }
            return String(chars)
        }

        fun vlq(): Long {
            var value = 0L
            repeat(4) {
                val byte = u8()
                value = (value shl 7) or (byte and 0x7F).toLong()
                if (byte and 0x80 == 0) {
                    return value
                }
            }
            throw PlaybackException("This MIDI file has a corrupt event.")
        }

        fun skip(count: Int) {
            if (count < 0 || position > limit - count) {
                throw PlaybackException("This MIDI file is truncated.")
            }
            position += count
        }

        fun rewindOne() {
            if (position <= 0) {
                throw PlaybackException("This MIDI file has a corrupt event.")
            }
            position--
        }
    }
}
