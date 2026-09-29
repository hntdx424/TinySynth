/*
 * JNI wrapper around TinySoundFont (tsf.h, MIT).
 * All calls for one tsf* happen under the Kotlin playback lock.
 */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

static tsf *from_handle(jlong handle) {
    return (tsf *)(intptr_t)handle;
}

static void init_channels(tsf *font) {
    int channel;
    for (channel = 0; channel < 16; channel++) {
        /* Channel 10 (index 9) follows General MIDI drum-bank rules. */
        tsf_channel_set_presetnumber(font, channel, 0, channel == 9);
    }
}

static int preset_bank(const tsf *font, int index) {
    if (index < 0 || index >= font->presetNum) {
        return 0;
    }
    return font->presets[index].bank;
}

static int preset_program(const tsf *font, int index) {
    if (index < 0 || index >= font->presetNum) {
        return 0;
    }
    return font->presets[index].preset;
}

static jstring sanitized_name(JNIEnv *env, const char *text) {
    char buffer[21];
    int i = 0;
    if (text == NULL) {
        text = "";
    }
    for (; text[i] != '\0' && i < 20; i++) {
        unsigned char c = (unsigned char)text[i];
        buffer[i] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    buffer[i] = '\0';
    return (*env)->NewStringUTF(env, buffer);
}

JNIEXPORT jlong JNICALL
Java_com_tinysynth_SoundFontSynth_nativeLoad(JNIEnv *env, jobject thiz, jbyteArray data) {
    jsize length;
    jbyte *bytes;
    tsf *font;
    (void)thiz;
    if (data == NULL) {
        return 0;
    }
    length = (*env)->GetArrayLength(env, data);
    if (length <= 0) {
        return 0;
    }
    bytes = (*env)->GetByteArrayElements(env, data, NULL);
    if (bytes == NULL) {
        return 0;
    }
    font = tsf_load_memory(bytes, (int)length);
    (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    if (font == NULL) {
        return 0;
    }
    tsf_set_max_voices(font, 384);
    init_channels(font);
    return (jlong)(intptr_t)font;
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeConfigure(
        JNIEnv *env, jobject thiz, jlong handle, jint sample_rate, jfloat gain_db) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || sample_rate <= 0) {
        return;
    }
    tsf_set_output(font, TSF_STEREO_INTERLEAVED, sample_rate, gain_db);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeClose(JNIEnv *env, jobject thiz, jlong handle) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font != NULL) {
        tsf_close(font);
    }
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeReset(JNIEnv *env, jobject thiz, jlong handle) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL) {
        return;
    }
    tsf_reset(font);
    init_channels(font);
}

JNIEXPORT jint JNICALL
Java_com_tinysynth_SoundFontSynth_nativePresetCount(JNIEnv *env, jobject thiz, jlong handle) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL) {
        return 0;
    }
    return tsf_get_presetcount(font);
}

JNIEXPORT jintArray JNICALL
Java_com_tinysynth_SoundFontSynth_nativePresetBankProgram(JNIEnv *env, jobject thiz, jlong handle) {
    tsf *font = from_handle(handle);
    int count;
    jintArray array;
    jint *values;
    int i;
    (void)thiz;
    if (font == NULL) {
        return NULL;
    }
    count = font->presetNum;
    array = (*env)->NewIntArray(env, count * 2);
    if (array == NULL) {
        return NULL;
    }
    values = (*env)->GetIntArrayElements(env, array, NULL);
    if (values == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        values[i * 2] = preset_bank(font, i);
        values[i * 2 + 1] = preset_program(font, i);
    }
    (*env)->ReleaseIntArrayElements(env, array, values, 0);
    return array;
}

JNIEXPORT jobjectArray JNICALL
Java_com_tinysynth_SoundFontSynth_nativePresetNames(JNIEnv *env, jobject thiz, jlong handle) {
    tsf *font = from_handle(handle);
    int count;
    jobjectArray array;
    jclass string_class;
    int i;
    (void)thiz;
    if (font == NULL) {
        return NULL;
    }
    count = font->presetNum;
    string_class = (*env)->FindClass(env, "java/lang/String");
    if (string_class == NULL) {
        return NULL;
    }
    array = (*env)->NewObjectArray(env, count, string_class, NULL);
    if (array == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        const char *name = tsf_get_presetname(font, i);
        jstring text = sanitized_name(env, name);
        if (text == NULL) {
            return NULL;
        }
        (*env)->SetObjectArrayElement(env, array, i, text);
        (*env)->DeleteLocalRef(env, text);
    }
    return array;
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeNoteOn(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint key, jfloat velocity) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15 || key < 0 || key > 127) {
        return;
    }
    tsf_channel_note_on(font, channel, key, velocity);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeNoteOff(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint key) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15 || key < 0 || key > 127) {
        return;
    }
    tsf_channel_note_off(font, channel, key);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeControl(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint controller, jint value) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15) {
        return;
    }
    tsf_channel_midi_control(font, channel, controller, value);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeProgram(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint program, jboolean drums) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15) {
        return;
    }
    tsf_channel_set_presetnumber(font, channel, program, drums ? 1 : 0);
}

JNIEXPORT jboolean JNICALL
Java_com_tinysynth_SoundFontSynth_nativeBankProgram(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint bank, jint program) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15) {
        return JNI_FALSE;
    }
    return tsf_channel_set_bank_preset(font, channel, bank, program) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativePitchBend(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jint value) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15) {
        return;
    }
    tsf_channel_set_pitchwheel(font, channel, value);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativePitchRange(
        JNIEnv *env, jobject thiz, jlong handle, jint channel, jfloat semitones) {
    tsf *font = from_handle(handle);
    (void)env;
    (void)thiz;
    if (font == NULL || channel < 0 || channel > 15) {
        return;
    }
    tsf_channel_set_pitchrange(font, channel, semitones);
}

JNIEXPORT void JNICALL
Java_com_tinysynth_SoundFontSynth_nativeRender(
        JNIEnv *env, jobject thiz, jlong handle, jshortArray buffer, jint offset_shorts, jint frames) {
    tsf *font = from_handle(handle);
    jsize length;
    jshort *samples;
    (void)thiz;
    if (font == NULL || buffer == NULL || frames <= 0 || offset_shorts < 0) {
        return;
    }
    length = (*env)->GetArrayLength(env, buffer);
    if ((jlong)offset_shorts + (jlong)frames * 2 > length) {
        return;
    }
    samples = (*env)->GetPrimitiveArrayCritical(env, buffer, NULL);
    if (samples == NULL) {
        return;
    }
    tsf_render_short(font, samples + offset_shorts, frames, 0);
    (*env)->ReleasePrimitiveArrayCritical(env, buffer, samples, 0);
}
