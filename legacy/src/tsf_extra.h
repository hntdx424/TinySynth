#ifndef TSF_EXTRA_H
#define TSF_EXTRA_H

/* Implemented in synth_impl.c against the vendored TinySoundFont preset table. */
#define TSF_NO_STDIO
#include "tsf.h"

int tsf_get_preset_bank(const tsf* font, int preset_index);
int tsf_get_preset_program(const tsf* font, int preset_index);

/* tsf_reset starts a short release. Stop, seek, and reload need the notes gone now. */
void tsf_kill_voices(tsf* font);

#endif
