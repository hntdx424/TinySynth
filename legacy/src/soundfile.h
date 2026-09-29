#ifndef SOUNDFILE_H
#define SOUNDFILE_H

/* Keep these byte-for-byte in sync with the wide strings in main.c. */
#define TINYSYNTH_DLS_MESSAGE "DLS soundfonts are not supported. Please choose an SF2 file."
#define TINYSYNTH_TYPE_MESSAGE "Only SoundFont 2 (.sf2) files are supported."
#define TINYSYNTH_SF_TITLE "Unsupported soundfont"

/* 0 = .sf2 (load it), 1 = .dls, 2 = any other extension. */
int soundfont_kind(const char* path);

#endif
