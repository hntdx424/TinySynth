#ifndef PLAYER_H
#define PLAYER_H

#define PLAYER_CHANNELS 16
#define PLAYER_SAMPLE_RATE 44100
#define PLAYER_MAX_VOICES 128

typedef struct Player Player;

typedef struct PlayerPreset {
    int bank;
    int program;
    char name[21];
} PlayerPreset;

/* All player_* calls on one Player must be serialized by the caller. */

Player* player_create(void);
void player_destroy(Player* player);

int player_load_soundfont(Player* player, const void* data, int size);
int player_load_midi(Player* player, const void* data, int size);

int player_has_soundfont(const Player* player);
int player_has_midi(const Player* player);
int player_preset_count(const Player* player);
const PlayerPreset* player_presets(const Player* player);

void player_set_override(Player* player, int channel, int enabled, int bank, int program);

void player_play(Player* player);
void player_pause(Player* player);
void player_stop(Player* player);
void player_seek_ms(Player* player, unsigned ms);
void player_mark_playing(Player* player);

int player_is_playing(const Player* player);
int player_is_paused(const Player* player);
int player_end_of_sequence(const Player* player);
unsigned player_position_ms(const Player* player);
unsigned player_duration_ms(const Player* player);

/* Interleaved stereo signed 16-bit. Advances the clock when the sequencer is active. */
void player_render(Player* player, short* interleaved, int frames);

#endif
