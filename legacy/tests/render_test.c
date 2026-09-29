#include "player.h"
#include "soundfile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void expect_true(int cond, const char* message)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures++;
    }
}

typedef struct ByteBuf {
    unsigned char* data;
    int len;
    int cap;
} ByteBuf;

static void buf_grow(ByteBuf* buf, int extra)
{
    if (buf->len + extra <= buf->cap)
        return;
    if (buf->cap < 64)
        buf->cap = 64;
    while (buf->len + extra > buf->cap)
        buf->cap *= 2;
    buf->data = (unsigned char*)realloc(buf->data, (size_t)buf->cap);
}

static void buf_bytes(ByteBuf* buf, const void* data, int n)
{
    buf_grow(buf, n);
    memcpy(buf->data + buf->len, data, (size_t)n);
    buf->len += n;
}

static void buf_u8(ByteBuf* buf, unsigned value)
{
    unsigned char byte = (unsigned char)value;
    buf_bytes(buf, &byte, 1);
}

static void buf_u16(ByteBuf* buf, unsigned value)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    buf_bytes(buf, bytes, 2);
}

static void buf_u32(ByteBuf* buf, unsigned value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
    buf_bytes(buf, bytes, 4);
}

static void buf_fourcc(ByteBuf* buf, const char* id)
{
    buf_bytes(buf, id, 4);
}

static void buf_name20(ByteBuf* buf, const char* name)
{
    char padded[20];
    memset(padded, 0, sizeof(padded));
    if (name)
        strncpy(padded, name, 19);
    buf_bytes(buf, padded, 20);
}

static void chunk(ByteBuf* dst, const char* id, const ByteBuf* payload)
{
    buf_fourcc(dst, id);
    buf_u32(dst, (unsigned)payload->len);
    buf_bytes(dst, payload->data, payload->len);
    if (payload->len & 1)
        buf_u8(dst, 0);
}

static void list_chunk(ByteBuf* dst, const char* type, const ByteBuf* children)
{
    buf_fourcc(dst, "LIST");
    buf_u32(dst, (unsigned)(4 + children->len));
    buf_fourcc(dst, type);
    buf_bytes(dst, children->data, children->len);
}

static void phdr(ByteBuf* buf, const char* name, unsigned preset, unsigned bank, unsigned bag)
{
    buf_name20(buf, name);
    buf_u16(buf, preset);
    buf_u16(buf, bank);
    buf_u16(buf, bag);
    buf_u32(buf, 0);
    buf_u32(buf, 0);
    buf_u32(buf, 0);
}

static void bag(ByteBuf* buf, unsigned gen, unsigned mod)
{
    buf_u16(buf, gen);
    buf_u16(buf, mod);
}

static void gen(ByteBuf* buf, unsigned oper, unsigned amount)
{
    buf_u16(buf, oper);
    buf_u16(buf, amount);
}

static void inst(ByteBuf* buf, const char* name, unsigned bag_index)
{
    buf_name20(buf, name);
    buf_u16(buf, bag_index);
}

static void shdr(ByteBuf* buf, const char* name, unsigned start, unsigned end,
    unsigned start_loop, unsigned end_loop, unsigned rate, unsigned pitch, unsigned type)
{
    buf_name20(buf, name);
    buf_u32(buf, start);
    buf_u32(buf, end);
    buf_u32(buf, start_loop);
    buf_u32(buf, end_loop);
    buf_u32(buf, rate);
    buf_u8(buf, pitch);
    buf_u8(buf, 0);
    buf_u16(buf, 0);
    buf_u16(buf, type);
}

static void mod_terminal(ByteBuf* buf)
{
    int i;
    for (i = 0; i < 10; i++)
        buf_u8(buf, 0);
}

/* One looping sine preset (bank 0 program 0) and one silent preset (bank 0 program 1). */
static int build_sf2(ByteBuf* out)
{
    enum { PERIOD = 168, TAIL = 46 };
    ByteBuf smpl = {0};
    ByteBuf pdta = {0};
    ByteBuf sdta_children = {0};
    ByteBuf body = {0};
    ByteBuf ph = {0}, pb = {0}, pm = {0}, pg = {0};
    ByteBuf ih = {0}, ib = {0}, im = {0}, ig = {0}, sh = {0};
    short sample;
    int i;
    unsigned sine_start = 0;
    unsigned silent_start = PERIOD + TAIL;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < PERIOD + TAIL; i++) {
        double x = sin(2.0 * 3.141592653589793 * (i % PERIOD) / (double)PERIOD);
        sample = (short)(x * 16000.0);
        buf_u16(&smpl, (unsigned)(unsigned short)sample);
    }
    for (i = 0; i < PERIOD + TAIL; i++)
        buf_u16(&smpl, 0);

    chunk(&sdta_children, "smpl", &smpl);

    phdr(&ph, "Sine", 0, 0, 0);
    phdr(&ph, "Silence", 1, 0, 1);
    phdr(&ph, "EOP", 0, 0, 2);
    bag(&pb, 0, 0);
    bag(&pb, 1, 0);
    bag(&pb, 2, 0);
    mod_terminal(&pm);
    gen(&pg, 41, 0);
    gen(&pg, 41, 1);
    inst(&ih, "Sine", 0);
    inst(&ih, "Silence", 1);
    inst(&ih, "EOI", 2);
    bag(&ib, 0, 0);
    bag(&ib, 2, 0);
    bag(&ib, 4, 0);
    mod_terminal(&im);
    gen(&ig, 54, 1);
    gen(&ig, 53, 0);
    gen(&ig, 54, 1);
    gen(&ig, 53, 1);
    shdr(&sh, "Sine", sine_start, sine_start + PERIOD - 1, sine_start, sine_start + PERIOD, 44100, 60, 1);
    shdr(&sh, "Silence", silent_start, silent_start + PERIOD - 1, silent_start, silent_start + PERIOD, 44100, 60, 1);
    shdr(&sh, "EOS", silent_start + PERIOD + TAIL, silent_start + PERIOD + TAIL, 0, 0, 0, 0, 0);
    chunk(&pdta, "phdr", &ph);
    chunk(&pdta, "pbag", &pb);
    chunk(&pdta, "pmod", &pm);
    chunk(&pdta, "pgen", &pg);
    chunk(&pdta, "inst", &ih);
    chunk(&pdta, "ibag", &ib);
    chunk(&pdta, "imod", &im);
    chunk(&pdta, "igen", &ig);
    chunk(&pdta, "shdr", &sh);

    list_chunk(&body, "sdta", &sdta_children);
    list_chunk(&body, "pdta", &pdta);
    buf_fourcc(out, "RIFF");
    buf_u32(out, (unsigned)(4 + body.len));
    buf_fourcc(out, "sfbk");
    buf_bytes(out, body.data, body.len);

    free(smpl.data);
    free(sdta_children.data);
    free(pdta.data);
    free(body.data);
    free(ph.data); free(pb.data); free(pm.data); free(pg.data);
    free(ih.data); free(ib.data); free(im.data); free(ig.data); free(sh.data);
    return out->data != NULL;
}

static void buf_u16be(ByteBuf* buf, unsigned value)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value >> 8);
    bytes[1] = (unsigned char)value;
    buf_bytes(buf, bytes, 2);
}

static void buf_u32be(ByteBuf* buf, unsigned value)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value >> 24);
    bytes[1] = (unsigned char)(value >> 16);
    bytes[2] = (unsigned char)(value >> 8);
    bytes[3] = (unsigned char)value;
    buf_bytes(buf, bytes, 4);
}

static int build_midi(ByteBuf* out)
{
    static const unsigned char track[] = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0xC0, 0x00,
        0x00, 0x90, 0x3C, 0x64,
        0x83, 0x60, 0x80, 0x3C, 0x00,
        0x00, 0xFF, 0x2F, 0x00
    };
    memset(out, 0, sizeof(*out));
    buf_bytes(out, "MThd", 4);
    buf_u32be(out, 6);
    buf_u16be(out, 0);
    buf_u16be(out, 1);
    buf_u16be(out, 480);
    buf_bytes(out, "MTrk", 4);
    buf_u32be(out, (unsigned)sizeof(track));
    buf_bytes(out, track, (int)sizeof(track));
    return 1;
}

static int peak_of(const short* samples, int frames)
{
    int peak = 0;
    int i;
    for (i = 0; i < frames * 2; i++) {
        int value = samples[i] < 0 ? -samples[i] : samples[i];
        if (value > peak)
            peak = value;
    }
    return peak;
}

static int render_peak(Player* player, int frames)
{
    short* buffer = (short*)calloc((size_t)frames * 2u, sizeof(short));
    int peak;
    player_render(player, buffer, frames);
    peak = peak_of(buffer, frames);
    free(buffer);
    return peak;
}

int main(void)
{
    ByteBuf sf2 = {0};
    ByteBuf midi = {0};
    Player* player;
    const PlayerPreset* presets;
    int frames = PLAYER_SAMPLE_RATE / 4;
    int sine_peak;
    int silent_peak;
    unsigned position;

    expect_true(strcmp(TINYSYNTH_DLS_MESSAGE, "DLS soundfonts are not supported. Please choose an SF2 file.") == 0,
        "DLS message text");
    expect_true(strcmp(TINYSYNTH_TYPE_MESSAGE, "Only SoundFont 2 (.sf2) files are supported.") == 0,
        "unsupported type message text");
    expect_true(soundfont_kind("C:/fonts/piano.sf2") == 0, ".sf2 kind");
    expect_true(soundfont_kind("piano.SF2") == 0, ".SF2 kind");
    expect_true(soundfont_kind("drum.dls") == 1, ".dls kind");
    expect_true(soundfont_kind("drum.DLS") == 1, ".DLS kind");
    expect_true(soundfont_kind("song.mid") == 2, "non-soundfont kind");
    expect_true(soundfont_kind("archive.sf2.bak") == 2, "trailing extension wins");

    expect_true(build_sf2(&sf2), "build sf2");
    expect_true(build_midi(&midi), "build midi");
    player = player_create();
    expect_true(player != NULL, "player_create");
    if (!player)
        return 1;

    expect_true(player_load_soundfont(player, "nope", 4) == 0, "reject garbage soundfont");
    expect_true(player_load_soundfont(player, sf2.data, sf2.len) == 1, "load generated sf2");
    expect_true(player_preset_count(player) == 2, "two presets");
    presets = player_presets(player);
    if (presets && player_preset_count(player) == 2) {
        expect_true(presets[0].bank == 0 && presets[0].program == 0, "sine preset order");
        expect_true(presets[1].bank == 0 && presets[1].program == 1, "silence preset order");
        expect_true(strcmp(presets[0].name, "Sine") == 0, "sine preset name");
    }
    expect_true(player_load_midi(player, "MIDI", 4) == 0, "reject garbage midi");
    expect_true(player_has_soundfont(player), "soundfont kept after bad midi");
    expect_true(player_load_midi(player, midi.data, midi.len) == 1, "load generated midi");
    expect_true(player_duration_ms(player) >= 400 && player_duration_ms(player) <= 700, "midi duration");

    player_play(player);
    sine_peak = render_peak(player, frames);
    expect_true(sine_peak > 1000, "sine preset renders audio");
    expect_true(player_position_ms(player) >= 200 && player_position_ms(player) <= 300, "position advances");

    player_pause(player);
    expect_true(player_is_paused(player) && !player_is_playing(player), "pause");
    position = player_position_ms(player);
    player_play(player);
    expect_true(player_is_playing(player) && !player_is_paused(player), "resume");
    expect_true(player_position_ms(player) == position, "resume keeps position");

    player_stop(player);
    expect_true(!player_is_playing(player) && player_position_ms(player) == 0, "stop rewinds");

    player_set_override(player, 0, 1, 0, 1);
    player_play(player);
    silent_peak = render_peak(player, frames);
    expect_true(silent_peak < 40, "channel override selects the silent preset");

    player_stop(player);
    player_set_override(player, 0, 0, 0, 0);
    player_seek_ms(player, 100);
    expect_true(player_position_ms(player) >= 80 && player_position_ms(player) <= 130, "seek position");
    sine_peak = render_peak(player, PLAYER_SAMPLE_RATE / 20);
    expect_true(sine_peak > 1000, "seek keeps the sounding note");

    player_destroy(player);
    free(sf2.data);
    free(midi.data);
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("render test ok\n");
    return 0;
}
