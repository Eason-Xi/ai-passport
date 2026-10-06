// tools/asmr_audio_preview.c —— 在电脑上离线渲染声景，供试听与响度校准。
//
// 用与固件相同的 as_sounds.c / as_mixer.c 生成 16 kHz 单声道 WAV：每种声景一个文件，
// 外加一个三层混音示例。不需要开发板。
//   cc -std=c11 -O2 -Imain tools/asmr_audio_preview.c main/as_dsp.c main/as_sounds.c \
//      main/as_mixer.c -lm -o build/asmr_audio_preview
//   build/asmr_audio_preview build/preview/audio 20
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "as_mixer.h"
#include "as_sounds.h"

#define BLOCK 240

static void put_u32(FILE *f, uint32_t v) {
    const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, uint16_t v) {
    const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

static FILE *wav_open(const char *path, uint32_t samples) {
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + samples * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 1);
    put_u32(f, AS_SAMPLE_RATE);
    put_u32(f, AS_SAMPLE_RATE * 2);
    put_u16(f, 2);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, samples * 2);
    return f;
}

static void write_block(FILE *f, const int16_t *pcm, size_t n) {
    for (size_t i = 0; i < n; i++) put_u16(f, (uint16_t)pcm[i]);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    const int seconds = argc > 2 ? atoi(argv[2]) : 20;
    const uint32_t total = (uint32_t)seconds * AS_SAMPLE_RATE / BLOCK * BLOCK;
    static int16_t pcm[BLOCK];
    char path[512];

    // 单个声景：直接渲染声部（与混音器层音量 10 档、主增益 1 等价，便于响度校准）。
    for (uint8_t id = 0; id < AS_SOUND_COUNT; id++) {
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, id, as_sound_debug_name(id));
        FILE *f = wav_open(path, total);
        if (!f) {
            perror(path);
            return 1;
        }
        static as_voice_t v;
        as_voice_init(&v, id, 1);
        for (uint32_t done = 0; done < total; done += BLOCK) {
            as_voice_render(&v, pcm, BLOCK);
            write_block(f, pcm, BLOCK);
        }
        fclose(f);
        printf("%s\n", path);
    }

    // 混音示例：细雨 8 + 篝火 5 + 虫鸣 3，含开头淡入。
    snprintf(path, sizeof path, "%s/mix_rain_fire_crickets.wav", dir);
    FILE *f = wav_open(path, total);
    if (!f) {
        perror(path);
        return 1;
    }
    static as_mixer_t mx;
    as_mixer_init(&mx);
    const uint8_t sounds[AS_LAYERS] = { AS_SOUND_RAIN, AS_SOUND_FIRE, AS_SOUND_CRICKETS };
    const uint8_t levels[AS_LAYERS] = { 8, 5, 3 };
    for (int i = 0; i < AS_LAYERS; i++) as_mixer_set_layer(&mx, i, sounds[i], levels[i]);
    as_mixer_set_playing(&mx, true);
    for (uint32_t done = 0; done < total; done += BLOCK) {
        as_mixer_render(&mx, pcm, BLOCK);
        write_block(f, pcm, BLOCK);
    }
    fclose(f);
    printf("%s\n", path);
    return 0;
}
