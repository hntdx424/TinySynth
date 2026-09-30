#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include "tsf.h"

#define TML_NO_STDIO
#define TML_IMPLEMENTATION
#include "tml.h"

int tsf_get_preset_bank(const struct tsf* font, int preset_index)
{
    if (!font || preset_index < 0 || preset_index >= font->presetNum)
        return 0;
    return (int)font->presets[preset_index].bank;
}

int tsf_get_preset_program(const struct tsf* font, int preset_index)
{
    if (!font || preset_index < 0 || preset_index >= font->presetNum)
        return 0;
    return (int)font->presets[preset_index].preset;
}

void tsf_kill_voices(struct tsf* font)
{
    int i;
    if (!font || !font->voices)
        return;
    for (i = 0; i < font->voiceNum; i++)
        font->voices[i].playingPreset = -1;
}
