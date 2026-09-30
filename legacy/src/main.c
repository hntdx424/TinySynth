/* TinySynth legacy UI. Win32 common controls only; no API newer than Windows XP. */

#if (_WIN32_WINNT) > 0x0501
#error This legacy build must be compiled with _WIN32_WINNT=0x0501 or lower.
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "audio.h"
#include "gm_names.h"
#include "soundfile.h"

#include <stdlib.h>
#include <stdio.h>

#define ID_TAB 100
#define ID_OPEN 101
#define ID_PLAY 102
#define ID_PAUSE 103
#define ID_STOP 104
#define ID_SF 105
#define ID_TIMER 1

/* Matches TinySynth/MainForm.cs, including the Unicode ellipsis and apostrophe. */
static const wchar_t* OPEN_LABEL = L"Open MIDI\u2026";
static const wchar_t* SF_LABEL = L"SoundFont\u2026";
static const wchar_t* HINT_TEXT =
    L"Assign an instrument to any of the 16 MIDI channels. "
    L"A selection remaps that channel\u2019s program and bank for the current or next playback. "
    L"Channel 10 is the standard drum channel.";
static const wchar_t* DLS_MESSAGE = L"DLS soundfonts are not supported. Please choose an SF2 file.";
static const wchar_t* TYPE_MESSAGE = L"Only SoundFont 2 (.sf2) files are supported.";

static const wchar_t MIDI_FILTER[] =
    L"MIDI files (*.mid;*.midi)\0*.mid;*.midi\0"
    L"All files (*.*)\0*.*\0";
static const wchar_t SF_FILTER[] =
    L"SoundFont (*.sf2;*.dls)\0*.sf2;*.dls\0"
    L"SoundFont 2 (*.sf2)\0*.sf2\0"
    L"Downloadable Sounds (*.dls)\0*.dls\0"
    L"All files (*.*)\0*.*\0";

static struct {
    HINSTANCE inst;
    HWND hwnd;
    HWND tab;
    HWND open_btn, play_btn, pause_btn, stop_btn, sf_btn;
    HWND seek, time_label;
    HWND midi_cap, midi_path, sf_cap, sf_path;
    HWND hint, panel;
    HWND ch_label[16];
    HWND ch_combo[16];
    HFONT font;
    int dragging;
    int filling;
    int scroll;
    wchar_t midi_file[MAX_PATH];
    wchar_t sf_file[MAX_PATH];
} g;

static int face_is(HFONT font, const wchar_t* name)
{
    HDC dc;
    HFONT old;
    wchar_t got[64];
    int same = 0;
    if (!font)
        return 0;
    dc = GetDC(NULL);
    old = (HFONT)SelectObject(dc, font);
    if (GetTextFaceW(dc, 64, got) > 0) {
        const wchar_t* a = got;
        const wchar_t* b = name;
        same = 1;
        while (*a || *b) {
            wchar_t ca = *a ? *a++ : 0;
            wchar_t cb = *b ? *b++ : 0;
            if (ca >= L'A' && ca <= L'Z')
                ca = (wchar_t)(ca - L'A' + L'a');
            if (cb >= L'A' && cb <= L'Z')
                cb = (wchar_t)(cb - L'A' + L'a');
            if (ca != cb) {
                same = 0;
                break;
            }
        }
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return same;
}

static HFONT make_font(void)
{
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    HFONT font;
    ReleaseDC(NULL, dc);
    if (dpi < 72)
        dpi = 96;
    font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!face_is(font, L"Segoe UI")) {
        if (font)
            DeleteObject(font);
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
    }
    return font;
}

static void set_font(HWND hwnd)
{
    if (hwnd && g.font)
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)g.font, TRUE);
}

static void info_box(const wchar_t* title, const wchar_t* text)
{
    MessageBoxW(g.hwnd, text, title, MB_OK | MB_ICONINFORMATION);
}

static void error_box(const wchar_t* title, const wchar_t* text)
{
    MessageBoxW(g.hwnd, text, title, MB_OK | MB_ICONERROR);
}

static int ext_is(const wchar_t* path, const wchar_t* ext)
{
    const wchar_t* dot = NULL;
    const wchar_t* p;
    if (!path || !ext)
        return 0;
    for (p = path; *p; p++) {
        if (*p == L'\\' || *p == L'/')
            dot = NULL;
        else if (*p == L'.')
            dot = p;
    }
    if (!dot)
        return 0;
    while (*dot && *ext) {
        wchar_t a = *dot++;
        wchar_t b = *ext++;
        if (a >= L'A' && a <= L'Z')
            a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z')
            b = (wchar_t)(b - L'A' + L'a');
        if (a != b)
            return 0;
    }
    return *dot == 0 && *ext == 0;
}

static int read_all(const wchar_t* path, void** out, int* out_size, const wchar_t** err)
{
    HANDLE file;
    DWORD high = 0;
    DWORD low;
    DWORD offset;
    void* buffer;
    *out = NULL;
    *out_size = 0;
    *err = L"The file could not be opened.";
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    low = GetFileSize(file, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        CloseHandle(file);
        *err = L"The file could not be read.";
        return 0;
    }
    if (high != 0 || low == 0 || low > 300u * 1024u * 1024u) {
        CloseHandle(file);
        *err = low == 0 ? L"The file is empty." : L"The file is too large.";
        return 0;
    }
    buffer = malloc(low);
    if (!buffer) {
        CloseHandle(file);
        *err = L"Not enough memory to read the file.";
        return 0;
    }
    offset = 0;
    while (offset < low) {
        DWORD chunk = 0;
        if (!ReadFile(file, (char*)buffer + offset, low - offset, &chunk, NULL) || chunk == 0) {
            free(buffer);
            CloseHandle(file);
            *err = L"The file could not be read.";
            return 0;
        }
        offset += chunk;
    }
    CloseHandle(file);
    *out = buffer;
    *out_size = (int)low;
    *err = NULL;
    return 1;
}

static void format_ms(wchar_t* dst, int cap, unsigned ms)
{
    unsigned seconds = ms / 1000u;
    _snwprintf(dst, (size_t)cap, L"%u:%02u", seconds / 60u, seconds % 60u);
    dst[cap - 1] = 0;
}

static void update_ui(void)
{
    AudioStatus status;
    wchar_t left[32];
    wchar_t right[32];
    wchar_t both[72];
    int slider = 0;
    audio_status(&status);
    SetWindowTextW(g.midi_path, g.midi_file[0] ? g.midi_file : L"No MIDI file loaded");
    SetWindowTextW(g.sf_path, g.sf_file[0] ? g.sf_file : L"No SoundFont loaded");
    EnableWindow(g.seek, status.has_midi);
    EnableWindow(g.play_btn, status.has_midi && status.has_soundfont && !status.playing);
    EnableWindow(g.pause_btn, status.playing);
    EnableWindow(g.stop_btn, status.playing || status.paused);
    if (g.dragging)
        return;
    if (status.duration_ms > 0) {
        unsigned long long scaled = (unsigned long long)status.position_ms * 1000ull / status.duration_ms;
        if (scaled > 1000ull)
            scaled = 1000ull;
        slider = (int)scaled;
    }
    SendMessageW(g.seek, TBM_SETPOS, TRUE, slider);
    format_ms(left, 32, status.position_ms);
    format_ms(right, 32, status.duration_ms);
    _snwprintf(both, 72, L"%s / %s", left, right);
    both[71] = 0;
    SetWindowTextW(g.time_label, both);
}

static void ansi_to_wide(const char* src, wchar_t* dst, int cap)
{
    int wrote;
    if (cap <= 0)
        return;
    dst[0] = 0;
    if (!src)
        return;
    wrote = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, cap);
    if (wrote <= 0)
        dst[0] = 0;
}

static void add_instrument(HWND box, int bank, int program, const wchar_t* name)
{
    wchar_t item[96];
    int index;
    unsigned packed;
    if (bank == 0)
        _snwprintf(item, 96, L"%03d  %s", program + 1, name);
    else
        _snwprintf(item, 96, L"%03d:%03d  %s", bank, program, name);
    item[95] = 0;
    index = (int)SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)item);
    if (index < 0)
        return;
    packed = ((unsigned)bank << 16) | (unsigned)program;
    SendMessageW(box, CB_SETITEMDATA, index, (LPARAM)packed);
}

static int find_instrument(HWND box, int bank, int program)
{
    int count = (int)SendMessageW(box, CB_GETCOUNT, 0, 0);
    int i;
    unsigned want = ((unsigned)bank << 16) | (unsigned)program;
    for (i = 1; i < count; i++) {
        LRESULT data = SendMessageW(box, CB_GETITEMDATA, i, 0);
        if ((unsigned)data == want)
            return i;
    }
    return 0;
}

static void refresh_instruments(void)
{
    int count = 0;
    PlayerPreset* presets = audio_dup_presets(&count);
    int channel;
    g.filling = 1;
    for (channel = 0; channel < 16; channel++) {
        HWND box = g.ch_combo[channel];
        int selected = (int)SendMessageW(box, CB_GETCURSEL, 0, 0);
        int keep = 0;
        int keep_bank = 0;
        int keep_program = 0;
        int index;
        int i;
        if (selected > 0) {
            LRESULT data = SendMessageW(box, CB_GETITEMDATA, selected, 0);
            if ((unsigned)data != 0xFFFFFFFFu) {
                keep = 1;
                keep_bank = (int)((unsigned)data >> 16);
                keep_program = (int)((unsigned)data & 0xFFFFu);
            }
        }
        SendMessageW(box, WM_SETREDRAW, FALSE, 0);
        SendMessageW(box, CB_RESETCONTENT, 0, 0);
        index = (int)SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)L"(from MIDI file)");
        if (index >= 0)
            SendMessageW(box, CB_SETITEMDATA, index, (LPARAM)0xFFFFFFFF);
        if (presets && count > 0) {
            for (i = 0; i < count; i++) {
                wchar_t name[64];
                ansi_to_wide(presets[i].name, name, 64);
                add_instrument(box, presets[i].bank, presets[i].program, name);
            }
        } else {
            for (i = 0; i < 128; i++) {
                wchar_t name[64];
                ansi_to_wide(GM_NAMES[i], name, 64);
                add_instrument(box, 0, i, name);
            }
        }
        SendMessageW(box, CB_SETCURSEL, keep ? find_instrument(box, keep_bank, keep_program) : 0, 0);
        SendMessageW(box, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(box, NULL, TRUE);
    }
    g.filling = 0;
    free(presets);
}

static void on_channel(int channel)
{
    int selected;
    LRESULT data;
    if (g.filling)
        return;
    selected = (int)SendMessageW(g.ch_combo[channel], CB_GETCURSEL, 0, 0);
    if (selected < 0)
        return;
    data = SendMessageW(g.ch_combo[channel], CB_GETITEMDATA, selected, 0);
    if ((unsigned)data == 0xFFFFFFFFu)
        audio_set_override(channel, 0, 0, 0);
    else
        audio_set_override(channel, 1, (int)((unsigned)data >> 16), (int)((unsigned)data & 0xFFFFu));
}

static int hint_height(int width)
{
    HDC dc;
    HFONT old;
    RECT rect;
    int height;
    if (width < 40)
        width = 40;
    dc = GetDC(g.hwnd);
    old = g.font ? (HFONT)SelectObject(dc, g.font) : NULL;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = 0;
    DrawTextW(dc, HINT_TEXT, -1, &rect, DT_WORDBREAK | DT_CALCRECT);
    if (g.font)
        SelectObject(dc, old);
    ReleaseDC(g.hwnd, dc);
    height = rect.bottom - rect.top + 4;
    if (height < 32)
        height = 32;
    return height;
}

static void layout_channels(void)
{
    RECT rect;
    int view;
    int content;
    int y0;
    int i;
    SCROLLINFO info;
    if (!g.panel)
        return;
    GetClientRect(g.panel, &rect);
    view = rect.bottom - rect.top;
    content = 8 + 16 * 32;
    if (content < 1)
        content = 1;
    if (g.scroll < 0)
        g.scroll = 0;
    if (view < content && g.scroll > content - view)
        g.scroll = content - view;
    if (view >= content)
        g.scroll = 0;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = content - 1;
    info.nPage = view > 0 ? (UINT)view : 1;
    info.nPos = g.scroll;
    SetScrollInfo(g.panel, SB_VERT, &info, TRUE);
    y0 = 4 - g.scroll;
    for (i = 0; i < 16; i++) {
        int y = y0 + i * 32;
        int combo_x = 158;
        int combo_w = rect.right - combo_x - 8;
        if (combo_w < 40)
            combo_w = 40;
        MoveWindow(g.ch_label[i], 8, y + 6, 146, 20, TRUE);
        MoveWindow(g.ch_combo[i], combo_x, y + 2, combo_w, 240, TRUE);
    }
}

static void layout_all(void)
{
    RECT client;
    RECT page;
    int player_page;
    int left;
    int top;
    int right;
    int bottom;
    int width;
    int y;
    int time_w = 124;
    int seek_w;
    int path_w;
    int hint_w;
    int hint_h;
    if (!g.hwnd || !g.tab)
        return;
    GetClientRect(g.hwnd, &client);
    MoveWindow(g.tab, 0, 0, client.right, client.bottom, TRUE);
    page = client;
    GetClientRect(g.tab, &page);
    TabCtrl_AdjustRect(g.tab, FALSE, &page);

    left = page.left + 16;
    top = page.top + 16;
    right = page.right - 16;
    bottom = page.bottom - 16;
    if (right < left + 200)
        right = left + 200;
    if (bottom < top + 120)
        bottom = top + 120;
    width = right - left;
    y = top;
    MoveWindow(g.open_btn, left, y, 120, 32, TRUE);
    MoveWindow(g.play_btn, left + 128, y, 88, 32, TRUE);
    MoveWindow(g.pause_btn, left + 224, y, 88, 32, TRUE);
    MoveWindow(g.stop_btn, left + 320, y, 88, 32, TRUE);
    y += 32 + 16;
    seek_w = width - time_w - 8;
    if (seek_w < 80)
        seek_w = 80;
    MoveWindow(g.seek, left, y, seek_w, 36, TRUE);
    MoveWindow(g.time_label, left + seek_w + 8, y + 8, time_w, 22, TRUE);
    y += 36 + 16;
    path_w = width - 96;
    if (path_w < 40)
        path_w = 40;
    MoveWindow(g.midi_cap, left, y, 90, 22, TRUE);
    MoveWindow(g.midi_path, left + 96, y, path_w, 22, TRUE);
    y += 26;
    MoveWindow(g.sf_cap, left, y, 90, 22, TRUE);
    MoveWindow(g.sf_path, left + 96, y, path_w, 22, TRUE);
    MoveWindow(g.sf_btn, right - 130, bottom - 32, 130, 32, TRUE);

    player_page = (TabCtrl_GetCurSel(g.tab) == 0);
    left = page.left + 12;
    top = page.top + 12;
    right = page.right - 12;
    bottom = page.bottom - 12;
    if (right < left + 200)
        right = left + 200;
    hint_w = right - left;
    hint_h = hint_height(hint_w);
    MoveWindow(g.hint, left, top, hint_w, hint_h, TRUE);
    MoveWindow(g.panel, left, top + hint_h + 10, hint_w, bottom - (top + hint_h + 10), TRUE);
    layout_channels();
    (void)player_page;
    /* The tab fills the client. Keep it behind the page controls. */
    SetWindowPos(g.tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void show_page(int index)
{
    int player = (index == 0);
    int i;
    HWND player_controls[11];
    player_controls[0] = g.open_btn;
    player_controls[1] = g.play_btn;
    player_controls[2] = g.pause_btn;
    player_controls[3] = g.stop_btn;
    player_controls[4] = g.sf_btn;
    player_controls[5] = g.seek;
    player_controls[6] = g.time_label;
    player_controls[7] = g.midi_cap;
    player_controls[8] = g.midi_path;
    player_controls[9] = g.sf_cap;
    player_controls[10] = g.sf_path;
    for (i = 0; i < 11; i++)
        ShowWindow(player_controls[i], player ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hint, player ? SW_HIDE : SW_SHOW);
    ShowWindow(g.panel, player ? SW_HIDE : SW_SHOW);
}

static void on_scroll(HWND panel, WPARAM wparam)
{
    SCROLLINFO info;
    int pos;
    int max_pos;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    GetScrollInfo(panel, SB_VERT, &info);
    pos = info.nPos;
    switch (LOWORD(wparam)) {
    case SB_TOP:
        pos = info.nMin;
        break;
    case SB_BOTTOM:
        pos = info.nMax;
        break;
    case SB_LINEUP:
        pos -= 32;
        break;
    case SB_LINEDOWN:
        pos += 32;
        break;
    case SB_PAGEUP:
        pos -= (int)info.nPage;
        break;
    case SB_PAGEDOWN:
        pos += (int)info.nPage;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        pos = info.nTrackPos;
        break;
    default:
        return;
    }
    if (pos < info.nMin)
        pos = info.nMin;
    max_pos = info.nMax - (int)info.nPage + 1;
    if (max_pos < 0)
        max_pos = 0;
    if (pos > max_pos)
        pos = max_pos;
    g.scroll = pos;
    layout_channels();
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_VSCROLL) {
        on_scroll(hwnd, wparam);
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetBkColor((HDC)wparam, GetSysColor(COLOR_WINDOW));
        SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    if (msg == WM_ERASEBKGND) {
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect((HDC)wparam, &rect, GetSysColorBrush(COLOR_WINDOW));
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void seek_from_slider(void)
{
    AudioStatus status;
    int value;
    unsigned ms = 0;
    audio_status(&status);
    value = (int)SendMessageW(g.seek, TBM_GETPOS, 0, 0);
    if (value < 0)
        value = 0;
    if (value > 1000)
        value = 1000;
    if (status.duration_ms > 0)
        ms = (unsigned)((unsigned long long)status.duration_ms * (unsigned)value / 1000ull);
    audio_seek(ms);
    g.dragging = 0;
    update_ui();
}

static void open_midi(void)
{
    wchar_t file[MAX_PATH];
    OPENFILENAMEW dialog;
    void* data = NULL;
    int size = 0;
    const wchar_t* err = NULL;
    HCURSOR previous;
    int ok;
    file[0] = 0;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g.hwnd;
    dialog.lpstrFilter = MIDI_FILTER;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Open MIDI file";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    dialog.lpstrDefExt = L"mid";
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError() != 0)
            error_box(L"Could not open MIDI file.", L"The file dialog could not be opened.");
        return;
    }
    if (!read_all(file, &data, &size, &err)) {
        error_box(L"Could not open MIDI file.", err ? err : L"The file could not be read.");
        return;
    }
    previous = SetCursor(LoadCursor(NULL, IDC_WAIT));
    ok = audio_load_midi(data, size);
    SetCursor(previous);
    free(data);
    if (!ok) {
        error_box(L"Could not open MIDI file.", L"The file could not be read as a Standard MIDI file.");
        return;
    }
    lstrcpynW(g.midi_file, file, MAX_PATH);
    update_ui();
}

static void open_soundfont(void)
{
    wchar_t file[MAX_PATH];
    OPENFILENAMEW dialog;
    void* data = NULL;
    int size = 0;
    const wchar_t* err = NULL;
    HCURSOR previous;
    int ok;
    file[0] = 0;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g.hwnd;
    dialog.lpstrFilter = SF_FILTER;
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Select SoundFont";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    dialog.lpstrDefExt = L"sf2";
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError() != 0)
            error_box(L"Could not load SoundFont.", L"The file dialog could not be opened.");
        return;
    }
    if (ext_is(file, L".dls")) {
        info_box(L"Unsupported soundfont", DLS_MESSAGE);
        return;
    }
    if (!ext_is(file, L".sf2")) {
        info_box(L"Unsupported soundfont", TYPE_MESSAGE);
        return;
    }
    if (!read_all(file, &data, &size, &err)) {
        error_box(L"Could not load SoundFont.", err ? err : L"The file could not be read.");
        return;
    }
    previous = SetCursor(LoadCursor(NULL, IDC_WAIT));
    ok = audio_load_soundfont(data, size);
    SetCursor(previous);
    free(data);
    if (!ok) {
        error_box(L"Could not load SoundFont.", L"The file could not be read as a SoundFont 2 file.");
        return;
    }
    lstrcpynW(g.sf_file, file, MAX_PATH);
    refresh_instruments();
    update_ui();
}

static void on_play(void)
{
    int result = audio_play();
    if (result == 1)
        info_box(L"TinySynth", L"Select a SoundFont (.sf2) before playing.");
    else if (result == 2)
        info_box(L"TinySynth", L"Open a MIDI file before playing.");
    else if (result == 3)
        error_box(L"Playback failed.", L"The waveOut audio device could not be opened.");
    update_ui();
}

static HWND make_button(HWND parent, int id, const wchar_t* text, int visible)
{
    HWND hwnd = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | BS_PUSHBUTTON | (visible ? WS_VISIBLE : 0),
        0, 0, 80, 32, parent, (HMENU)(INT_PTR)id, g.inst, NULL);
    set_font(hwnd);
    return hwnd;
}

static HWND make_label(HWND parent, const wchar_t* text, DWORD style, int visible)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_CLIPSIBLINGS | style | (visible ? WS_VISIBLE : 0),
        0, 0, 40, 20, parent, NULL, g.inst, NULL);
    set_font(hwnd);
    return hwnd;
}

static void create_controls(HWND hwnd)
{
    TCITEMW item;
    int i;
    g.tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)ID_TAB, g.inst, NULL);
    set_font(g.tab);
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = L"Player";
    SendMessageW(g.tab, TCM_INSERTITEMW, 0, (LPARAM)&item);
    item.pszText = L"Channel instruments";
    SendMessageW(g.tab, TCM_INSERTITEMW, 1, (LPARAM)&item);

    g.open_btn = make_button(hwnd, ID_OPEN, OPEN_LABEL, 1);
    g.play_btn = make_button(hwnd, ID_PLAY, L"Play", 1);
    g.pause_btn = make_button(hwnd, ID_PAUSE, L"Pause", 1);
    g.stop_btn = make_button(hwnd, ID_STOP, L"Stop", 1);
    g.sf_btn = make_button(hwnd, ID_SF, SF_LABEL, 1);
    g.seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS,
        0, 0, 100, 36, hwnd, (HMENU)(INT_PTR)200, g.inst, NULL);
    SendMessageW(g.seek, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
    SendMessageW(g.seek, TBM_SETPAGESIZE, 0, 25);
    g.time_label = make_label(hwnd, L"0:00 / 0:00", SS_RIGHT | SS_NOPREFIX, 1);
    g.midi_cap = make_label(hwnd, L"MIDI", SS_LEFT | SS_NOPREFIX, 1);
    g.midi_path = make_label(hwnd, L"No MIDI file loaded", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 1);
    g.sf_cap = make_label(hwnd, L"SoundFont", SS_LEFT | SS_NOPREFIX, 1);
    g.sf_path = make_label(hwnd, L"No SoundFont loaded", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 1);

    g.hint = CreateWindowExW(0, L"EDIT", HINT_TEXT,
        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_LEFT,
        0, 0, 10, 10, hwnd, NULL, g.inst, NULL);
    set_font(g.hint);
    g.panel = CreateWindowExW(WS_EX_CLIENTEDGE, L"TinySynthChannels", L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL,
        0, 0, 10, 10, hwnd, NULL, g.inst, NULL);
    for (i = 0; i < 16; i++) {
        wchar_t caption[40];
        if (i == 9)
            _snwprintf(caption, 40, L"Channel %d  (drums)", i + 1);
        else
            _snwprintf(caption, 40, L"Channel %d", i + 1);
        caption[39] = 0;
        g.ch_label[i] = make_label(g.panel, caption, SS_LEFT | SS_NOPREFIX, 1);
        g.ch_combo[i] = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, 100, 240, g.panel, NULL, g.inst, NULL);
        set_font(g.ch_combo[i]);
    }
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        create_controls(hwnd);
        refresh_instruments();
        layout_all();
        show_page(0);
        update_ui();
        SetTimer(hwnd, ID_TIMER, 80, NULL);
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
            layout_all();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* info = (MINMAXINFO*)lparam;
        info->ptMinTrackSize.x = 720;
        info->ptMinTrackSize.y = 500;
        return 0;
    }
    case WM_TIMER:
        if (wparam == ID_TIMER) {
            AudioStatus status;
            audio_status(&status);
            if (status.playing && status.ended)
                audio_stop();
            update_ui();
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam);
        int code = HIWORD(wparam);
        if (code == BN_CLICKED) {
            if (id == ID_OPEN)
                open_midi();
            else if (id == ID_PLAY)
                on_play();
            else if (id == ID_PAUSE) {
                audio_pause();
                update_ui();
            } else if (id == ID_STOP) {
                audio_stop();
                update_ui();
            } else if (id == ID_SF)
                open_soundfont();
        } else if (code == CBN_SELCHANGE && !g.filling) {
            HWND box = (HWND)lparam;
            int channel;
            for (channel = 0; channel < 16; channel++) {
                if (box == g.ch_combo[channel])
                    on_channel(channel);
            }
        }
        return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lparam == g.seek) {
            int code = LOWORD(wparam);
            if (code == TB_THUMBTRACK)
                g.dragging = 1;
            else if (code == TB_ENDTRACK || code == TB_THUMBPOSITION || code == TB_LINEUP ||
                     code == TB_LINEDOWN || code == TB_PAGEUP || code == TB_PAGEDOWN ||
                     code == TB_TOP || code == TB_BOTTOM)
                seek_from_slider();
        }
        return 0;
    case WM_NOTIFY:
        if (((LPNMHDR)lparam)->hwndFrom == g.tab && ((LPNMHDR)lparam)->code == TCN_SELCHANGE) {
            show_page(TabCtrl_GetCurSel(g.tab));
            layout_all();
        }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wparam;
        HWND child = (HWND)lparam;
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        if (child == g.midi_cap || child == g.sf_cap)
            SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        else
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)wparam, GetSysColor(COLOR_BTNFACE));
        SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        audio_shutdown();
        if (g.font)
            DeleteObject(g.font);
        g.font = NULL;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    INITCOMMONCONTROLSEX controls;
    WNDCLASSW main_class;
    WNDCLASSW panel_class;
    HWND hwnd;
    MSG message;
    int x;
    int y;
    (void)prev;
    (void)cmd;
    g.inst = inst;
    if (!audio_init()) {
        MessageBoxW(NULL, L"Could not start the audio thread.", L"TinySynth", MB_OK | MB_ICONERROR);
        return 1;
    }
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);
    g.font = make_font();

    memset(&panel_class, 0, sizeof(panel_class));
    panel_class.lpfnWndProc = panel_proc;
    panel_class.hInstance = inst;
    panel_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    panel_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    panel_class.lpszClassName = L"TinySynthChannels";
    RegisterClassW(&panel_class);

    memset(&main_class, 0, sizeof(main_class));
    main_class.lpfnWndProc = main_proc;
    main_class.hInstance = inst;
    main_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    main_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    main_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    main_class.lpszClassName = L"TinySynthLegacy";
    RegisterClassW(&main_class);

    x = (GetSystemMetrics(SM_CXSCREEN) - 780) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - 540) / 2;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    hwnd = CreateWindowExW(0, L"TinySynthLegacy", L"TinySynth",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, 780, 540, NULL, NULL, inst, NULL);
    if (!hwnd) {
        audio_shutdown();
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return (int)message.wParam;
}
