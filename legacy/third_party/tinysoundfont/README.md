# TinySoundFont and TinyMidiLoader

Vendored from [schellingb/TinySoundFont](https://github.com/schellingb/TinySoundFont) commit `853a0a171759f1ddba0de1442133a75912bbeffa` (2026-07-19).

| File | License |
|---|---|
| `tsf.h` | MIT. Copyright (C) 2017-2025 Bernhard Schelling. Based on SFZero, Copyright (C) 2012 Steve Folta. |
| `tml.h` | zlib. Copyright (C) 2017, 2018, 2020 Bernhard Schelling. |

The license text is at the top of each header. The headers are unmodified.

`src/synth_impl.c` compiles both implementations and adds three helpers against this version's internal tables. `tsf_get_preset_bank` and `tsf_get_preset_program` read the bank and program of a preset index, which the public API does not expose. `tsf_kill_voices` stops notes immediately; `tsf_reset` only starts a short release.
