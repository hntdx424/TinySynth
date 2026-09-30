#include "player.h"

#include "tsf_extra.h"

#define TML_NO_STDIO
#include "tml.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct Player {
    tsf* font;
    tml_message* midi;
    tml_message* cursor;
    PlayerPreset* presets;
    int preset_count;
    int default_preset;
    int sample_rate;
    unsigned long long position_samples;
    unsigned duration_ms;
    int playing;
    int paused;
    int active;
    int bank[PLAYER_CHANNELS];
    int program[PLAYER_CHANNELS];
    struct {
        int enabled;
        int bank;
        int program;
    } over[PLAYER_CHANNELS];
};

static int preset_cmp(const void* left, const void* right)
{
    const PlayerPreset* a = (const PlayerPreset*)left;
    const PlayerPreset* b = (const PlayerPreset*)right;
    int i;
    if (a->bank != b->bank)
        return (a->bank > b->bank) - (a->bank < b->bank);
    if (a->program != b->program)
        return (a->program > b->program) - (a->program < b->program);
    for (i = 0; a->name[i] || b->name[i]; i++) {
        int ca = tolower((unsigned char)a->name[i]);
        int cb = tolower((unsigned char)b->name[i]);
        if (ca != cb)
            return ca - cb;
    }
    return 0;
}

static void select_preset(Player* player, int channel, int bank, int program)
{
    tsf* font;
    int index;
    if (!player->font || (unsigned)channel >= PLAYER_CHANNELS)
        return;
    font = player->font;
    if (bank < 0)
        bank = 0;
    if (program < 0)
        program = 0;
    if (program > 127)
        program = 127;
    index = tsf_get_presetindex(font, bank, program);
    if (index < 0) {
        if (bank < 128)
            index = tsf_get_presetindex(font, 0, program);
        else
            index = tsf_get_presetindex(font, 128, 0);
    }
    if (index < 0)
        index = player->default_preset;
    tsf_channel_set_bank(font, channel, bank);
    if (index >= 0)
        tsf_channel_set_presetindex(font, channel, index);
}

/* MeltySynth treats channel 10 as percussion: bank select is stored as value+128,
   and the channel starts on bank 128. Match that so GM files play drums. */
static void set_bank_value(Player* player, int channel, int value)
{
    int bank = value;
    if (channel == 9)
        bank += 128;
    if (bank < 0)
        bank = 0;
    player->bank[channel] = bank;
    select_preset(player, channel, bank, player->program[channel]);
}

static void apply_override_now(Player* player, int channel)
{
    int bank = player->over[channel].bank;
    if (channel == 9)
        bank += 128;
    player->bank[channel] = bank;
    player->program[channel] = player->over[channel].program;
    select_preset(player, channel, player->bank[channel], player->program[channel]);
}

static void silence_and_rearm(Player* player)
{
    int channel;
    if (!player->font)
        return;
    tsf_reset(player->font);
    tsf_kill_voices(player->font);
    tsf_set_output(player->font, TSF_STEREO_INTERLEAVED, player->sample_rate, 0.0f);
    tsf_set_volume(player->font, 0.5f);
    for (channel = 0; channel < PLAYER_CHANNELS; channel++) {
        player->bank[channel] = (channel == 9) ? 128 : 0;
        player->program[channel] = 0;
        select_preset(player, channel, player->bank[channel], 0);
    }
    player->cursor = player->midi;
    player->position_samples = 0;
}

static void rebuild_presets(Player* player)
{
    int count;
    int i;
    unsigned best_id = 0xFFFFFFFFu;
    PlayerPreset* list;
    free(player->presets);
    player->presets = NULL;
    player->preset_count = 0;
    player->default_preset = -1;
    if (!player->font)
        return;
    count = tsf_get_presetcount(player->font);
    if (count <= 0)
        return;
    list = (PlayerPreset*)calloc((size_t)count, sizeof(PlayerPreset));
    if (!list)
        return;
    for (i = 0; i < count; i++) {
        unsigned id;
        const char* name = tsf_get_presetname(player->font, i);
        list[i].bank = tsf_get_preset_bank(player->font, i);
        list[i].program = tsf_get_preset_program(player->font, i);
        if (!name)
            name = "";
        memcpy(list[i].name, name, 20);
        list[i].name[20] = 0;
        id = ((unsigned)list[i].bank << 16) | (unsigned)list[i].program;
        if (id < best_id) {
            best_id = id;
            player->default_preset = i;
        }
    }
    qsort(list, (size_t)count, sizeof(PlayerPreset), preset_cmp);
    player->presets = list;
    player->preset_count = count;
}

static void dispatch(Player* player, const tml_message* message)
{
    int channel = message->channel;
    int type = message->type;
    if ((unsigned)channel >= PLAYER_CHANNELS || !player->font)
        return;

    if (type == TML_PROGRAM_CHANGE && player->over[channel].enabled) {
        apply_override_now(player, channel);
        return;
    }

    if (type == TML_CONTROL_CHANGE) {
        int control = (unsigned char)message->control;
        int value = (unsigned char)message->control_value;
        if (player->over[channel].enabled && (control == 0 || control == 32)) {
            if (control == 0)
                set_bank_value(player, channel, player->over[channel].bank);
            return;
        }
        if (control == 32)
            return;
        if (control == 0) {
            set_bank_value(player, channel, value);
            return;
        }
        if (control == 121) {
            tsf_channel_midi_control(player->font, channel, 121, value);
            select_preset(player, channel, player->bank[channel], player->program[channel]);
            return;
        }
        tsf_channel_midi_control(player->font, channel, control, value);
        return;
    }

    switch (type) {
    case TML_NOTE_ON: {
        int key = (unsigned char)message->key;
        int velocity = (unsigned char)message->velocity;
        if (velocity == 0)
            tsf_channel_note_off(player->font, channel, key);
        else
            tsf_channel_note_on(player->font, channel, key, velocity / 127.0f);
        break;
    }
    case TML_NOTE_OFF:
        tsf_channel_note_off(player->font, channel, (unsigned char)message->key);
        break;
    case TML_PROGRAM_CHANGE:
        player->program[channel] = (unsigned char)message->program;
        select_preset(player, channel, player->bank[channel], player->program[channel]);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(player->font, channel, message->pitch_bend);
        break;
    default:
        break;
    }
}

Player* player_create(void)
{
    Player* player = (Player*)calloc(1, sizeof(Player));
    if (!player)
        return NULL;
    player->sample_rate = PLAYER_SAMPLE_RATE;
    player->default_preset = -1;
    return player;
}

void player_destroy(Player* player)
{
    if (!player)
        return;
    if (player->font)
        tsf_close(player->font);
    if (player->midi)
        tml_free(player->midi);
    free(player->presets);
    free(player);
}

int player_load_soundfont(Player* player, const void* data, int size)
{
    tsf* font;
    if (!player || !data || size <= 0)
        return 0;
    font = tsf_load_memory(data, size);
    if (!font)
        return 0;
    if (player->font)
        tsf_close(player->font);
    player->font = font;
    rebuild_presets(player);
    tsf_set_output(font, TSF_STEREO_INTERLEAVED, player->sample_rate, 0.0f);
    tsf_set_volume(font, 0.5f);
    tsf_set_max_voices(font, PLAYER_MAX_VOICES);
    silence_and_rearm(player);
    return 1;
}

int player_load_midi(Player* player, const void* data, int size)
{
    tml_message* midi;
    unsigned length = 0;
    if (!player || !data || size <= 0)
        return 0;
    midi = tml_load_memory(data, size);
    if (!midi)
        return 0;
    if (player->midi)
        tml_free(player->midi);
    player->midi = midi;
    player->playing = 0;
    player->paused = 0;
    player->active = 0;
    tml_get_info(midi, NULL, NULL, NULL, NULL, &length);
    player->duration_ms = length;
    if (player->font)
        silence_and_rearm(player);
    else {
        player->cursor = midi;
        player->position_samples = 0;
    }
    return 1;
}

int player_has_soundfont(const Player* player)
{
    return player && player->font != NULL;
}

int player_has_midi(const Player* player)
{
    return player && player->midi != NULL;
}

int player_preset_count(const Player* player)
{
    return player ? player->preset_count : 0;
}

const PlayerPreset* player_presets(const Player* player)
{
    return player ? player->presets : NULL;
}

void player_set_override(Player* player, int channel, int enabled, int bank, int program)
{
    if (!player || (unsigned)channel >= PLAYER_CHANNELS)
        return;
    player->over[channel].enabled = enabled ? 1 : 0;
    player->over[channel].bank = bank;
    player->over[channel].program = program;
    if (enabled && player->font && (player->playing || player->paused))
        apply_override_now(player, channel);
}

void player_play(Player* player)
{
    if (!player || !player->font || !player->midi)
        return;
    if (player->paused) {
        player->paused = 0;
        player->playing = 1;
        player->active = 1;
        return;
    }
    silence_and_rearm(player);
    player->active = 1;
    player->playing = 1;
    player->paused = 0;
}

void player_pause(Player* player)
{
    if (!player || !player->playing)
        return;
    player->playing = 0;
    player->paused = 1;
}

void player_stop(Player* player)
{
    if (!player)
        return;
    player->playing = 0;
    player->paused = 0;
    player->active = 0;
    if (player->font)
        silence_and_rearm(player);
    else {
        player->cursor = player->midi;
        player->position_samples = 0;
    }
}

void player_seek_ms(Player* player, unsigned ms)
{
    int was_playing;
    int was_paused;
    unsigned long long target;
    short discard[256 * 2];
    if (!player || !player->font || !player->midi)
        return;
    if (ms > player->duration_ms)
        ms = player->duration_ms;
    was_playing = player->playing;
    was_paused = player->paused;
    silence_and_rearm(player);
    player->active = 1;
    target = (unsigned long long)ms * (unsigned)player->sample_rate / 1000ull;
    while (player->position_samples < target) {
        unsigned long long before = player->position_samples;
        int frames = (int)(target - player->position_samples);
        if (frames > 256)
            frames = 256;
        if (frames <= 0)
            break;
        player_render(player, discard, frames);
        if (player->position_samples <= before)
            break;
    }
    player->playing = was_playing;
    player->paused = was_paused;
}

void player_mark_playing(Player* player)
{
    if (!player)
        return;
    player->playing = 1;
    player->paused = 0;
    player->active = 1;
}

int player_is_playing(const Player* player)
{
    return player && player->playing;
}

int player_is_paused(const Player* player)
{
    return player && player->paused;
}

int player_end_of_sequence(const Player* player)
{
    return player && player->active && player->cursor == NULL;
}

unsigned player_position_ms(const Player* player)
{
    if (!player || player->sample_rate <= 0)
        return 0;
    return (unsigned)(player->position_samples * 1000ull / (unsigned)player->sample_rate);
}

unsigned player_duration_ms(const Player* player)
{
    return player ? player->duration_ms : 0;
}

void player_render(Player* player, short* interleaved, int frames)
{
    int done = 0;
    int spins = 0;
    if (!interleaved || frames <= 0)
        return;
    if (!player || !player->font || !player->active) {
        memset(interleaved, 0, (size_t)frames * 2u * sizeof(short));
        return;
    }
    while (done < frames) {
        int batch;
        if (++spins > frames + 256)
            break;
        batch = frames - done;
        if (player->cursor) {
            unsigned long long event_sample =
                (unsigned long long)player->cursor->time * (unsigned)player->sample_rate / 1000ull;
            if (event_sample > player->position_samples) {
                unsigned long long gap = event_sample - player->position_samples;
                if (gap < (unsigned long long)batch)
                    batch = (int)gap;
            } else {
                while (player->cursor) {
                    unsigned long long due =
                        (unsigned long long)player->cursor->time * (unsigned)player->sample_rate / 1000ull;
                    if (due > player->position_samples)
                        break;
                    dispatch(player, player->cursor);
                    player->cursor = player->cursor->next;
                }
                continue;
            }
        }
        if (batch <= 0)
            break;
        tsf_render_short(player->font, interleaved + done * 2, batch, 0);
        player->position_samples += (unsigned long long)batch;
        done += batch;
    }
    if (done < frames)
        memset(interleaved + done * 2, 0, (size_t)(frames - done) * 2u * sizeof(short));
}
