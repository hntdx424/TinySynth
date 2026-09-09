namespace TinySynth.Playback;

/// <summary>
/// A selectable instrument: either a SoundFont preset or a General MIDI fallback.
/// </summary>
public readonly record struct InstrumentChoice(int Bank, int Program, string Name)
{
    public string DisplayName =>
        Bank == 0
            ? $"{Program + 1:000}  {Name}"
            : $"{Bank:000}:{Program:000}  {Name}";

    public override string ToString() => DisplayName;
}
