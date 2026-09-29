# TinySynth for Android

A small Android MIDI player that renders Standard MIDI files through a SoundFont 2 (`.sf2`) synthesizer. It follows the Windows TinySynth app: a **Player** tab and a **Channel instruments** tab.

Playback does **not** use Android’s built-in MIDI player. SoundFonts are rendered with [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIT), compiled into the app with the NDK. Samples are written to an `AudioTrack` (`44.1 kHz`, stereo, 16-bit).

`.dls` files can be chosen in the soundfont picker. They are rejected with a clear message, same as the Windows app. Only `.sf2` is loaded.

## Requirements

- JDK 17 or newer
- Android SDK platform 35, build-tools 35.0.0
- Android NDK `27.2.12479018`
- CMake `3.22.1`
- A device or emulator running Android 7.0 (API 24) or newer
- A SoundFont 2 file (`.sf2`)

If those SDK packages are not installed yet:

```bash
sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.2.12479018" "cmake;3.22.1"
```

Tell Gradle where the SDK is. Either export `ANDROID_HOME` or create `android/local.properties` (this file is not committed):

```
sdk.dir=/path/to/Android/sdk
```

## Build

From the `android/` directory:

```bash
./gradlew assembleDebug
```

The debug APK is:

```
android/app/build/outputs/apk/debug/app-debug.apk
```

## Install

With a device or emulator connected:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Using the app

1. Open the **Player** tab.
2. Tap **Open MIDI…** and choose a `.mid` or `.midi` file (the system file picker).
3. Tap **SoundFont…** at the bottom-right and choose a `.sf2` file.
4. Tap **Play**. Use **Pause**, **Stop**, and the seek bar as needed.

The Player tab shows the loaded MIDI file name and SoundFont name. **Play** stays disabled until both are loaded. If you press play without one of them, or the file cannot be read, the app shows a message and leaves the previous song alone.

## Channel remapping

The **Channel instruments** tab lists MIDI channels 1–16. Channel 10 is labeled drums.

- Before a SoundFont is loaded, each dropdown lists the 128 General MIDI instrument names.
- After an `.sf2` is loaded, the lists are filled from that font’s presets. Bank 0 presets are shown as `001  Name` (General MIDI program number). Other banks are shown as `bank:program  Name`.
- **(from MIDI file)** leaves the channel alone, so bank and program events in the file are used.
- Picking an instrument remaps that channel’s bank and program for the current playback and the next one. Changing a dropdown while a file is playing applies on the next notes immediately.
- While an override is active, bank-select and program-change events from the file for that channel are replaced by the chosen preset.
- Seeking rebuilds each channel (program, controllers, and notes still held at that time) and then applies any overrides again, so the chosen instruments stay in effect.

## Tests

Parser, seek/chase, and file-type checks run on the JVM (no device):

```bash
./gradlew testDebugUnitTest
```

## Project layout

```
android/
  app/src/main/java/com/tinysynth/   # UI, MIDI parser, playback, channel chase
  app/src/main/cpp/                  # TinySoundFont + JNI
  app/src/main/res/                  # Player and Channel instruments layouts
  app/src/test/                      # JVM tests for parsing and remapping
```

TinySoundFont’s MIT license is included in `app/src/main/cpp/tsf.h`.
