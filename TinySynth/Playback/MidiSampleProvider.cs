using MeltySynth;
using NAudio.Wave;

namespace TinySynth.Playback;

/// <summary>
/// Streams MeltySynth output into NAudio. All sequencer / synth access is
/// serialized through <see cref="Sync"/> so the UI thread can seek, remap
/// channels, and stop without racing the audio callback.
/// </summary>
internal sealed class MidiSampleProvider : ISampleProvider
{
    public const int SampleRate = 44100;

    private static readonly WaveFormat Format = WaveFormat.CreateIeeeFloatWaveFormat(SampleRate, 2);

    private readonly Synthesizer _synthesizer;
    private readonly MidiFileSequencer _sequencer;
    private readonly InstrumentChoice?[] _overrides;
    private readonly float[] _seekLeft = new float[512];
    private readonly float[] _seekRight = new float[512];

    public MidiSampleProvider(SoundFont soundFont, InstrumentChoice?[] overrides, object sync)
    {
        Sync = sync;
        _overrides = overrides;
        _synthesizer = new Synthesizer(soundFont, SampleRate);
        _sequencer = new MidiFileSequencer(_synthesizer)
        {
            OnSendMessage = DispatchMessage
        };
    }

    public object Sync { get; }

    public WaveFormat WaveFormat => Format;

    public TimeSpan Position
    {
        get
        {
            lock (Sync)
            {
                return _sequencer.Position;
            }
        }
    }

    public bool EndOfSequence
    {
        get
        {
            lock (Sync)
            {
                return _sequencer.EndOfSequence;
            }
        }
    }

    public void Play(MidiFile midiFile)
    {
        lock (Sync)
        {
            _sequencer.Play(midiFile, loop: false);
        }
    }

    public void Stop()
    {
        lock (Sync)
        {
            _sequencer.Stop();
        }
    }

    public void Seek(MidiFile midiFile, TimeSpan position)
    {
        lock (Sync)
        {
            _sequencer.Play(midiFile, loop: false);

            var frames = (int)Math.Max(0, position.TotalSeconds * SampleRate);
            while (frames > 0)
            {
                var chunk = Math.Min(frames, _seekLeft.Length);
                _sequencer.Render(_seekLeft.AsSpan(0, chunk), _seekRight.AsSpan(0, chunk));
                frames -= chunk;
            }
        }
    }

    /// <summary>
    /// Immediately apply a bank + program change (used when the user picks
    /// an instrument while something is already playing).
    /// </summary>
    public void ApplyOverrideNow(int channel, InstrumentChoice instrument)
    {
        lock (Sync)
        {
            SendProgram(_synthesizer, channel, instrument);
        }
    }

    public int Read(float[] buffer, int offset, int count)
    {
        lock (Sync)
        {
            _sequencer.RenderInterleaved(buffer.AsSpan(offset, count));
        }

        return count;
    }

    private void DispatchMessage(Synthesizer synthesizer, int channel, int command, int data1, int data2)
    {
        if ((uint)channel < 16 && _overrides[channel] is { } remap)
        {
            // Program change — substitute the user-chosen patch.
            if (command == 0xC0)
            {
                SendProgram(synthesizer, channel, remap);
                return;
            }

            // Bank select from the MIDI file would fight the override.
            if (command == 0xB0 && (data1 == 0 || data1 == 32))
            {
                data2 = data1 == 0 ? remap.Bank : 0;
            }
        }

        synthesizer.ProcessMidiMessage(channel, command, data1, data2);
    }

    private static void SendProgram(Synthesizer synthesizer, int channel, InstrumentChoice instrument)
    {
        synthesizer.ProcessMidiMessage(channel, 0xB0, 0, instrument.Bank);
        synthesizer.ProcessMidiMessage(channel, 0xB0, 32, 0);
        synthesizer.ProcessMidiMessage(channel, 0xC0, instrument.Program, 0);
    }
}
