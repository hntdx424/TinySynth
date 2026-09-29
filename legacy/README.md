# TinySynth for Windows XP, Vista, and 7

A 32-bit player with the same controls as the .NET 8 app in the repository root: Open MIDI, Play, Pause, Stop, a seek bar with time, the loaded MIDI and SoundFont names, and a **Channel instruments** tab. SoundFont 2 files play. DLS files are rejected with the same message as the .NET app.

The root app targets `net8.0-windows`. .NET 8 does not run on Windows XP, Vista, or 7, so this build is separate and does not change that project.

## Why this stack

Native C99 and Win32, cross-compiled with MinGW-w64 to a statically linked x86 executable.

- The executable is a PE32 GUI binary whose operating-system and subsystem versions are 5.1, which is Windows XP. It links `msvcrt.dll`, the C runtime that ships with XP, and does not use the Universal CRT, `api-ms-win-*` forwarders, or Vista-only calls such as `FlsAlloc`, SRW locks, or condition variables.
- No .NET Framework install is required. .NET Framework 4.0 is the last framework that supports XP, but a managed port would still need a framework setup on the machine and a synth library that actually builds for `net40`. A single exe is easier to copy onto an old PC.
- Synthesis is [TinySoundFont](https://github.com/schellingb/TinySoundFont) (`tsf.h`) and MIDI parsing is TinyMidiLoader (`tml.h`), both vendored as single headers under `third_party/tinysoundfont/`. Audio output is `waveOut` (four buffers of 1024 stereo frames at 44100 Hz). The window uses common controls: a tab, buttons, a trackbar, and combo boxes.

The compiler is the i686 MinGW-w64 **win32** thread model (`i686-w64-mingw32-gcc-win32` when it is installed). That toolchain does not pull in `libwinpthread`. Ubuntu's GCC 13 build of this toolchain does not accept `-mcrtdll=msvcrt`; its default spec already links `msvcrt.dll`, which `make test` checks.

## Supported systems

| System | Expected |
|---|---|
| Windows XP SP3, 32-bit | This is the target. The PE subsystem is 5.01. |
| Windows Vista and 7, 32-bit or 64-bit | A 32-bit exe runs on the 64-bit editions through WoW64. |
| Later Windows | Should run, but the root .NET 8 app is the one to use there. |

This repository was not built or executed on a real XP, Vista, or 7 machine. What was checked is described at the bottom.

## Build

From Linux, with the i686 MinGW-w64 toolchain and Python 3:

```bash
sudo apt install gcc-mingw-w64-i686 binutils-mingw-w64-i686
make -C legacy
```

That writes `legacy/build/TinySynth.exe`.

`make -C legacy test` rebuilds the exe, checks that it is a 32-bit GUI binary with subsystem 5.1 and an XP-safe import table (`tools/check_xp_imports.py`), and runs a host render test. The render test does not need Wine or Windows. It builds a tiny SoundFont (a looping sine and a silent preset) and a format-0 MIDI file, then checks that the sine renders, pause and stop behave, a channel override selects the silent preset, and seeking keeps the sounding note.

`make -C legacy clean` removes `legacy/build/`.

## Run

Copy `TinySynth.exe` to the machine. No installer and no extra DLLs are required beyond what XP SP3 already has (`kernel32`, `user32`, `gdi32`, `comctl32`, `comdlg32`, `winmm`, `msvcrt`).

1. **Open MIDI…** and choose a `.mid` or `.midi` file.
2. **SoundFont…** (bottom right) and choose a `.sf2` file. The dialog also lists `.dls` files. Choosing one shows *DLS soundfonts are not supported. Please choose an SF2 file.* Any other extension shows *Only SoundFont 2 (.sf2) files are supported.*
3. **Play**. **Pause** holds the position. **Stop** returns to the start and silences the notes. The slider seeks while the button is down and applies when it is released.

**Channel instruments** lists channels 1–16. Channel 10 is labeled drums. Until a SoundFont is loaded, each dropdown shows the 128 General MIDI names. After a font loads, it lists that font's presets. **(from MIDI file)** keeps the song's bank and program. Any other choice overrides that channel, including while playback is paused or running.

The window uses Segoe UI when that font is installed, and Tahoma otherwise (Windows XP does not include Segoe UI).

## Playback behavior matched to the .NET app

- Sample rate 44100 Hz, stereo, master volume 0.5, up to 128 voices.
- Bank select is MIDI controller 0. Controller 32 (bank LSB) is ignored.
- Channel 10 is percussion: its bank is stored as the selected value plus 128, and it starts on bank 128, matching MeltySynth. A preset chosen for channel 10 is looked up in that offset bank.
- If a bank and program are missing, a melodic channel falls back to bank 0 with the same program, and a percussion channel falls back to bank 128 program 0. Otherwise the lowest bank/program in the font is used.
- Preset labels are `001  Name` on bank 0 (the program number shown is one-based) and `bank:program  Name` otherwise.

## Limitations

- DLS is rejected. SoundFont 3 / Vorbis samples are not compiled in.
- TinySoundFont does not apply SoundFont modulators, chorus, or reverb. The timbre is not the same as MeltySynth.
- Seeking renders from the start of the file up to the target time, so a long seek can hitch.
- Files larger than 300 MB are refused. Paths are limited to `MAX_PATH`.
- The UI is unthemed Win32 common controls (comctl32 5.x, no side-by-side v6 manifest), so it is not pixel-identical to the WinForms layout. A v6 manifest can refuse to start when that assembly is missing, and under Wine it left the controls unpainted.

## What was verified

On the Linux build machine:

- `make -C legacy test` produces `build/TinySynth.exe`, `objdump -p` reports `pei-i386`, OS version 5.1, and GUI subsystem 5.1.
- The import table contains only `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `COMCTL32.dll`, `COMDLG32.DLL`, `WINMM.DLL`, and `msvcrt.dll`. None of the Vista-and-later functions checked by `tools/check_xp_imports.py` are imported (`FlsAlloc`, `GetTickCount64`, SRW locks, condition variables, `InitializeCriticalSectionEx`, thread-pool APIs, `SetProcessDPIAware`, and the others listed in that script).
- The host render test loads a generated SoundFont, renders audible sine samples from a generated MIDI file, and checks pause, stop, seek, and a per-channel preset override.
- A separate host run loaded TinySoundFont's example `florestan-subset.sf2` (17 presets) and `venture.mid` (48 seconds) and rendered one second of audio with a peak of 7437. Those files were downloaded for the check and are not in this repository.

The window was also opened under Wine 9 (32-bit prefix) on this Linux machine. The Player tab shows Open MIDI, Play, Pause, Stop, the seek bar, `0:00 / 0:00`, and the empty MIDI and SoundFont lines, with SoundFont at the bottom right. The Channel instruments tab shows the same hint as the .NET app, channels 1–16 with channel 10 labeled drums, and dropdowns that start with `(from MIDI file)` and then the General MIDI names. Play stays disabled until both a MIDI file and a SoundFont are loaded, matching the .NET app.

Not verified: a real Windows XP, Vista, or 7 machine; choosing a file in the Wine file dialog; or hearing `waveOut` through Wine. Audio was checked with the host render test above, not through the Windows audio device.
