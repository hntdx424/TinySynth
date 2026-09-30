#ifndef AUDIO_H
#define AUDIO_H

#include "player.h"

typedef struct AudioStatus {
    int has_soundfont;
    int has_midi;
    int playing;
    int paused;
    int ended;
    unsigned position_ms;
    unsigned duration_ms;
} AudioStatus;

int audio_init(void);
void audio_shutdown(void);

void audio_status(AudioStatus* status);
PlayerPreset* audio_dup_presets(int* count);

/* 0 on success. 1 = no SoundFont, 2 = no MIDI, 3 = waveOut failed. */
int audio_play(void);
void audio_pause(void);
void audio_stop(void);
void audio_seek(unsigned ms);

int audio_load_soundfont(const void* data, int size);
int audio_load_midi(const void* data, int size);
void audio_set_override(int channel, int enabled, int bank, int program);

#endif
