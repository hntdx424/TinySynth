# Cross-build a 32-bit TinySynth.exe for Windows XP SP3 and later.
# Requires a MinGW-w64 i686 toolchain that uses the win32 thread model and msvcrt
# (the CRT that ships with Windows XP), not the Universal CRT.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)/build

CC_WIN ?= $(shell command -v i686-w64-mingw32-gcc-win32 2>/dev/null || command -v i686-w64-mingw32-gcc)
CC_HOST ?= gcc
OBJDUMP ?= i686-w64-mingw32-objdump

WIN_CPPFLAGS := \
	-DWINVER=0x0501 \
	-D_WIN32_WINNT=0x0501 \
	-D_WIN32_IE=0x0501 \
	-DNTDDI_VERSION=0x05010000 \
	-DUNICODE -D_UNICODE \
	-DWIN32_LEAN_AND_MEAN

WIN_CFLAGS := -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter $(WIN_CPPFLAGS) \
	-Isrc -Ithird_party/tinysoundfont

WIN_LDFLAGS := -mwindows -municode -static -s \
	-Wl,--subsystem,windows:5.01 \
	-Wl,--major-os-version,5,--minor-os-version,1 \
	-Wl,--major-subsystem-version,5,--minor-subsystem-version,1 \
	-lcomctl32 -lcomdlg32 -lwinmm -lgdi32 -luser32 -lkernel32

HOST_CFLAGS := -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
	-Isrc -Ithird_party/tinysoundfont

.PHONY: all clean test verify

all: $(BUILD)/TinySynth.exe

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/synth_impl.o: src/synth_impl.c third_party/tinysoundfont/tsf.h third_party/tinysoundfont/tml.h | $(BUILD)
	$(CC_WIN) -std=c99 -O2 -w $(WIN_CPPFLAGS) -Ithird_party/tinysoundfont -c src/synth_impl.c -o $@

$(BUILD)/player.o: src/player.c src/player.h src/tsf_extra.h | $(BUILD)
	$(CC_WIN) $(WIN_CFLAGS) -c src/player.c -o $@

$(BUILD)/audio.o: src/audio.c src/audio.h src/player.h | $(BUILD)
	$(CC_WIN) $(WIN_CFLAGS) -c src/audio.c -o $@

$(BUILD)/main.o: src/main.c src/audio.h src/gm_names.h src/soundfile.h | $(BUILD)
	$(CC_WIN) $(WIN_CFLAGS) -c src/main.c -o $@

$(BUILD)/TinySynth.exe: $(BUILD)/main.o $(BUILD)/audio.o $(BUILD)/player.o $(BUILD)/synth_impl.o
	$(CC_WIN) -o $@ $(BUILD)/main.o $(BUILD)/audio.o $(BUILD)/player.o $(BUILD)/synth_impl.o $(WIN_LDFLAGS)

$(BUILD)/render_test: tests/render_test.c src/player.c src/synth_impl.c src/soundfile.c \
		src/player.h src/soundfile.h src/tsf_extra.h third_party/tinysoundfont/tsf.h third_party/tinysoundfont/tml.h | $(BUILD)
	$(CC_HOST) $(HOST_CFLAGS) -o $@ tests/render_test.c src/player.c src/synth_impl.c src/soundfile.c -lm

verify: $(BUILD)/TinySynth.exe
	python3 tools/check_xp_imports.py $(BUILD)/TinySynth.exe $(OBJDUMP)

test: $(BUILD)/render_test verify
	$(BUILD)/render_test

clean:
	rm -rf $(BUILD)
