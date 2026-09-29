#include "audio.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdlib.h>
#include <string.h>

#define AUDIO_BUFFERS 4
#define AUDIO_FRAMES 1024

static CRITICAL_SECTION g_lock;
static HANDLE g_wake;
static HANDLE g_thread;
static HWAVEOUT g_wave;
static Player* g_player;
static volatile int g_quit;
static int g_ready;

static short* g_samples[AUDIO_BUFFERS];
static WAVEHDR g_headers[AUDIO_BUFFERS];
static int g_submitted[AUDIO_BUFFERS];

static void service_locked(void)
{
    int i;
    if (!g_wave || !player_is_playing(g_player))
        return;
    for (i = 0; i < AUDIO_BUFFERS; i++) {
        WAVEHDR* header = &g_headers[i];
        MMRESULT result;
        if (g_submitted[i]) {
            if (header->dwFlags & WHDR_DONE)
                g_submitted[i] = 0;
            else
                continue;
        }
        player_render(g_player, g_samples[i], AUDIO_FRAMES);
        header->dwBufferLength = AUDIO_FRAMES * 2 * (DWORD)sizeof(short);
        header->dwFlags &= ~WHDR_DONE;
        result = waveOutWrite(g_wave, header, sizeof(*header));
        if (result == MMSYSERR_NOERROR)
            g_submitted[i] = 1;
    }
}

static DWORD WINAPI audio_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(g_wake, 40);
        if (g_quit)
            break;
        EnterCriticalSection(&g_lock);
        if (!g_quit)
            service_locked();
        LeaveCriticalSection(&g_lock);
    }
    return 0;
}

static void drop_queued_locked(void)
{
    int i;
    if (!g_wave)
        return;
    waveOutReset(g_wave);
    for (i = 0; i < AUDIO_BUFFERS; i++)
        g_submitted[i] = 0;
}

static int open_device_locked(void)
{
    WAVEFORMATEX format;
    MMRESULT result;
    int i;
    if (g_wave)
        return 1;
    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = PLAYER_SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = PLAYER_SAMPLE_RATE * 4;
    result = waveOutOpen(&g_wave, WAVE_MAPPER, &format, (DWORD_PTR)g_wake, 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        g_wave = NULL;
        return 0;
    }
    for (i = 0; i < AUDIO_BUFFERS; i++) {
        g_samples[i] = (short*)calloc((size_t)AUDIO_FRAMES * 2u, sizeof(short));
        if (!g_samples[i])
            return 0;
        memset(&g_headers[i], 0, sizeof(g_headers[i]));
        g_headers[i].lpData = (LPSTR)g_samples[i];
        g_headers[i].dwBufferLength = AUDIO_FRAMES * 2 * (DWORD)sizeof(short);
        g_headers[i].dwUser = (DWORD_PTR)i;
        if (waveOutPrepareHeader(g_wave, &g_headers[i], sizeof(g_headers[i])) != MMSYSERR_NOERROR)
            return 0;
        g_submitted[i] = 0;
    }
    return 1;
}

int audio_init(void)
{
    if (g_ready)
        return 1;
    g_player = player_create();
    if (!g_player)
        return 0;
    InitializeCriticalSection(&g_lock);
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!g_wake)
        return 0;
    g_quit = 0;
    g_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    if (!g_thread)
        return 0;
    SetThreadPriority(g_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    g_ready = 1;
    return 1;
}

void audio_shutdown(void)
{
    int i;
    if (!g_ready)
        return;
    g_quit = 1;
    if (g_wake)
        SetEvent(g_wake);
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_wave) {
        waveOutReset(g_wave);
        for (i = 0; i < AUDIO_BUFFERS; i++) {
            if (g_headers[i].lpData)
                waveOutUnprepareHeader(g_wave, &g_headers[i], sizeof(g_headers[i]));
            free(g_samples[i]);
            g_samples[i] = NULL;
        }
        waveOutClose(g_wave);
        g_wave = NULL;
    }
    player_destroy(g_player);
    g_player = NULL;
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = NULL;
    }
    DeleteCriticalSection(&g_lock);
    g_ready = 0;
}

void audio_status(AudioStatus* status)
{
    memset(status, 0, sizeof(*status));
    if (!g_player)
        return;
    EnterCriticalSection(&g_lock);
    status->has_soundfont = player_has_soundfont(g_player);
    status->has_midi = player_has_midi(g_player);
    status->playing = player_is_playing(g_player);
    status->paused = player_is_paused(g_player);
    status->ended = player_end_of_sequence(g_player);
    status->position_ms = player_position_ms(g_player);
    status->duration_ms = player_duration_ms(g_player);
    LeaveCriticalSection(&g_lock);
}

PlayerPreset* audio_dup_presets(int* count)
{
    int n;
    PlayerPreset* copy = NULL;
    *count = 0;
    if (!g_player)
        return NULL;
    EnterCriticalSection(&g_lock);
    n = player_preset_count(g_player);
    if (n > 0 && player_presets(g_player)) {
        copy = (PlayerPreset*)malloc((size_t)n * sizeof(PlayerPreset));
        if (copy) {
            memcpy(copy, player_presets(g_player), (size_t)n * sizeof(PlayerPreset));
            *count = n;
        }
    }
    LeaveCriticalSection(&g_lock);
    return copy;
}

int audio_play(void)
{
    int need_open;
    if (!g_player)
        return 3;
    EnterCriticalSection(&g_lock);
    if (!player_has_soundfont(g_player)) {
        LeaveCriticalSection(&g_lock);
        return 1;
    }
    if (!player_has_midi(g_player)) {
        LeaveCriticalSection(&g_lock);
        return 2;
    }
    need_open = (g_wave == NULL);
    player_play(g_player);
    if (need_open && !open_device_locked()) {
        player_stop(g_player);
        LeaveCriticalSection(&g_lock);
        return 3;
    }
    if (g_wave)
        waveOutRestart(g_wave);
    service_locked();
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
    return 0;
}

void audio_pause(void)
{
    if (!g_player)
        return;
    EnterCriticalSection(&g_lock);
    if (player_is_playing(g_player)) {
        player_pause(g_player);
        if (g_wave)
            waveOutPause(g_wave);
    }
    LeaveCriticalSection(&g_lock);
}

void audio_stop(void)
{
    if (!g_player)
        return;
    EnterCriticalSection(&g_lock);
    player_stop(g_player);
    drop_queued_locked();
    LeaveCriticalSection(&g_lock);
}

void audio_seek(unsigned ms)
{
    int playing;
    if (!g_player)
        return;
    EnterCriticalSection(&g_lock);
    playing = player_is_playing(g_player);
    player_seek_ms(g_player, ms);
    if (g_wave && (playing || player_is_paused(g_player))) {
        drop_queued_locked();
        if (playing)
            service_locked();
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
}

int audio_load_soundfont(const void* data, int size)
{
    int was_playing;
    int has_midi;
    unsigned position;
    int ok;
    if (!g_player)
        return 0;
    EnterCriticalSection(&g_lock);
    was_playing = player_is_playing(g_player);
    has_midi = player_has_midi(g_player);
    position = player_position_ms(g_player);
    ok = player_load_soundfont(g_player, data, size);
    if (!ok) {
        LeaveCriticalSection(&g_lock);
        return 0;
    }
    if (was_playing && has_midi) {
        player_seek_ms(g_player, position);
        player_mark_playing(g_player);
    } else {
        player_stop(g_player);
    }
    if (g_wave) {
        drop_queued_locked();
        if (player_is_playing(g_player))
            service_locked();
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
    return 1;
}

int audio_load_midi(const void* data, int size)
{
    int ok;
    if (!g_player)
        return 0;
    EnterCriticalSection(&g_lock);
    ok = player_load_midi(g_player, data, size);
    if (ok && g_wave)
        drop_queued_locked();
    LeaveCriticalSection(&g_lock);
    return ok;
}

void audio_set_override(int channel, int enabled, int bank, int program)
{
    if (!g_player)
        return;
    EnterCriticalSection(&g_lock);
    player_set_override(g_player, channel, enabled, bank, program);
    LeaveCriticalSection(&g_lock);
}
