#pragma once
#include <stdint.h>

#define AUDIO_RATE 32000

enum {
    S_SHOOT, S_HIT, S_KILL, S_CAW, S_DASH, S_COIN, S_SPARK, S_BUILD, S_DENY, S_HURT,
    S_WAVE, S_UNLOCK, S_FAIL, S_HEAL, S_TRAP, S_UI, S_DIG, S_BOSS, S_TICK, S_COUNT
};

void audio_init(void);
void sfx_play(int id, float vol);
void audio_set_enabled(int sfx, int music);
void audio_set_intensity(float v);            /* 0 = calm (building), 1 = battle */
void audio_mix(int16_t *out, int frames);     /* interleaved stereo int16 */
