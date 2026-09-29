#include "soundfile.h"

#include <ctype.h>

static int ext_eq(const char* path, const char* ext)
{
    const char* dot;
    if (!path || !ext)
        return 0;
    dot = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            dot = p + 1;
    }
    const char* last = 0;
    for (const char* p = dot; *p; p++) {
        if (*p == '.')
            last = p;
    }
    if (!last)
        return 0;
    while (*last && *ext) {
        unsigned char a = (unsigned char)*last++;
        unsigned char b = (unsigned char)*ext++;
        if (tolower(a) != tolower(b))
            return 0;
    }
    return *last == 0 && *ext == 0;
}

int soundfont_kind(const char* path)
{
    if (ext_eq(path, ".sf2"))
        return 0;
    if (ext_eq(path, ".dls"))
        return 1;
    return 2;
}
