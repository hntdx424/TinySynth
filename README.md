# TinySynth

A small Windows MIDI player that renders Standard MIDI files through a SoundFont 2 (`.sf2`) synthesizer. Playback uses [MeltySynth](https://github.com/sinshu/meltysynth) for SF2 synthesis and [NAudio](https://github.com/naudio/NAudio) for audio output — not the Windows MIDI mapper.

## Requirements

- Windows
- [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)
- A SoundFont 2 file (`.sf2`). DLS appears in the file dialog but is not supported.

## Build

From the repository root:

```bash
dotnet build
```

The app targets `net8.0-windows` (WinForms). A Release build:

```bash
dotnet build -c Release
```

## Run on Windows

```bash
dotnet run --project TinySynth
```

Or launch `TinySynth.exe` from `TinySynth/bin/Debug/net8.0-windows/` (or `Release`).

1. Click **Open MIDI…** and choose a `.mid` / `.midi` file.
2. Click **SoundFont…** (bottom-right) and choose a `.sf2` file.
3. Press **Play**. Use **Pause**, **Stop**, and the seek slider as needed.

The Player tab shows the loaded MIDI and SoundFont paths.

## Channel remapping

The **Channel instruments** tab lists MIDI channels 1–16 (channel 10 is labeled drums).

- Before a SoundFont is loaded, each dropdown lists the 128 General MIDI instrument names.
- After an `.sf2` is loaded, the lists are filled from that font’s presets (`bank:program` plus the preset name).
- **(from MIDI file)** leaves the channel alone so program/bank events in the file are used.
- Picking an instrument remaps that channel’s bank select and program change for the current or next playback. Changing a dropdown while a file is playing applies immediately.

## NuGet dependencies

| Package | Version | Role |
|---|---|---|
| [MeltySynth](https://www.nuget.org/packages/MeltySynth/2.4.1) | 2.4.1 | SF2 synthesizer and MIDI sequencer |
| [NAudio](https://www.nuget.org/packages/NAudio/2.2.1) | 2.2.1 | WaveOut audio output (`WaveOutEvent`) |

Restore happens automatically with `dotnet build` / `dotnet run`.

## Project layout

```
TinySynth.sln
TinySynth/
  TinySynth.csproj          # net8.0-windows WinForms
  MainForm.cs               # Player + Channel instruments UI
  Playback/                 # MeltySynth + NAudio engine
```

DLS (`.dls`) is offered in the SoundFont dialog so typical libraries are visible, but MeltySynth only loads SF2. Selecting a DLS file shows a clear message and does not change the current font.
