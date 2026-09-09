using MeltySynth;
using NAudio.Wave;

namespace TinySynth.Playback;

/// <summary>
/// Owns the SoundFont, MIDI file, NAudio output device, and per-channel
/// instrument overrides. The UI talks only to this type.
/// </summary>
public sealed class MidiPlaybackEngine : IDisposable
{
    private readonly object _sync = new();
    private readonly InstrumentChoice?[] _overrides = new InstrumentChoice?[16];

    private SoundFont? _soundFont;
    private MidiFile? _midiFile;
    private MidiSampleProvider? _provider;
    private WaveOutEvent? _output;
    private bool _paused;
    private bool _disposed;

    public string? MidiPath { get; private set; }
    public string? SoundFontPath { get; private set; }

    public bool HasMidi => _midiFile is not null;
    public bool HasSoundFont => _soundFont is not null;
    public bool IsPlaying => _output?.PlaybackState == PlaybackState.Playing;
    public bool IsPaused => _paused;

    public TimeSpan Duration => _midiFile?.Length ?? TimeSpan.Zero;

    public TimeSpan Position => _provider?.Position ?? TimeSpan.Zero;

    public bool EndOfSequence => _provider is { EndOfSequence: true };

    /// <summary>
    /// Presets from the loaded SoundFont, or General MIDI names if none is loaded.
    /// </summary>
    public IReadOnlyList<InstrumentChoice> AvailableInstruments { get; private set; } = GeneralMidi.Instruments;

    public void LoadMidi(string path)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        var midi = new MidiFile(path);
        StopOutput();
        _provider?.Stop();
        _midiFile = midi;
        MidiPath = path;
        _paused = false;
    }

    public void LoadSoundFont(string path)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        var extension = Path.GetExtension(path);
        if (extension.Equals(".dls", StringComparison.OrdinalIgnoreCase))
        {
            throw new NotSupportedException(
                "DLS soundfonts are not supported. Please choose an SF2 file.");
        }

        if (!extension.Equals(".sf2", StringComparison.OrdinalIgnoreCase))
        {
            throw new NotSupportedException(
                "Only SoundFont 2 (.sf2) files are supported.");
        }

        var font = new SoundFont(path);
        var presets = font.Presets
            .Select(p => new InstrumentChoice(p.BankNumber, p.PatchNumber, p.Name))
            .OrderBy(p => p.Bank)
            .ThenBy(p => p.Program)
            .ThenBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        var wasPlaying = IsPlaying;
        var resumeAt = Position;
        StopOutput();
        DisposeOutput();

        _soundFont = font;
        SoundFontPath = path;
        AvailableInstruments = presets.Length > 0 ? presets : GeneralMidi.Instruments;
        _provider = new MidiSampleProvider(font, _overrides, _sync);

        if (wasPlaying && _midiFile is not null)
        {
            EnsureOutput();
            _provider.Seek(_midiFile, resumeAt);
            _output!.Play();
            _paused = false;
        }
    }

    public void Play()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        if (_soundFont is null || _provider is null)
        {
            throw new InvalidOperationException("Select a SoundFont (.sf2) before playing.");
        }

        if (_midiFile is null)
        {
            throw new InvalidOperationException("Open a MIDI file before playing.");
        }

        EnsureOutput();

        if (_paused && _output!.PlaybackState == PlaybackState.Paused)
        {
            _output.Play();
            _paused = false;
            return;
        }

        _provider.Play(_midiFile);
        _output!.Play();
        _paused = false;
    }

    public void Pause()
    {
        if (_output is null || _output.PlaybackState != PlaybackState.Playing)
        {
            return;
        }

        _output.Pause();
        _paused = true;
    }

    public void Stop()
    {
        StopOutput();
        _provider?.Stop();
        _paused = false;
    }

    public void Seek(TimeSpan position)
    {
        if (_midiFile is null || _provider is null)
        {
            return;
        }

        var clamped = position < TimeSpan.Zero
            ? TimeSpan.Zero
            : position > Duration ? Duration : position;

        var resume = IsPlaying || _paused;
        _provider.Seek(_midiFile, clamped);

        if (resume && _output is not null && _output.PlaybackState != PlaybackState.Playing && !_paused)
        {
            _output.Play();
        }
    }

    public void SetChannelOverride(int channel, InstrumentChoice? instrument)
    {
        if ((uint)channel >= 16)
        {
            throw new ArgumentOutOfRangeException(nameof(channel));
        }

        lock (_sync)
        {
            _overrides[channel] = instrument;
        }

        if (instrument is { } choice && _provider is not null && (IsPlaying || _paused))
        {
            _provider.ApplyOverrideNow(channel, choice);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        StopOutput();
        DisposeOutput();
        _soundFont = null;
        _midiFile = null;
    }

    private void EnsureOutput()
    {
        if (_output is not null || _provider is null)
        {
            return;
        }

        var output = new WaveOutEvent
        {
            DesiredLatency = 100
        };
        output.Init(_provider);
        _output = output;
    }

    private void StopOutput()
    {
        // WaveOutEvent.Stop waits for the audio callback. Never hold _sync here —
        // Read() already holds that lock.
        _output?.Stop();
    }

    private void DisposeOutput()
    {
        _output?.Dispose();
        _output = null;
        _provider = null;
    }
}
