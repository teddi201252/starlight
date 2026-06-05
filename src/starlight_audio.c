#include "starlight_internal.h"

#include <SDL2/SDL_mixer.h>
#include <stdio.h>

static Mix_Music *g_startup_music = NULL;
static Mix_Music *g_copy_effect_music = NULL;
static int g_audio_open = 0;

void sl_audio_init(void) {
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) != 0) {
        fprintf(stderr, "starlight: Mix_OpenAudio failed: %s\n", Mix_GetError());
        return;
    }

    g_audio_open = 1;
    Mix_VolumeMusic(MIX_MAX_VOLUME);

    g_startup_music = Mix_LoadMUS(SL_RESOURCE_START_WAV);
    if (!g_startup_music) {
        fprintf(stderr, "starlight: Mix_LoadMUS failed for '%s': %s\n",
                SL_RESOURCE_START_WAV, Mix_GetError());
    }

    g_copy_effect_music = Mix_LoadMUS(SL_RESOURCE_COPYEFF_WAV);
    if (!g_copy_effect_music) {
        fprintf(stderr, "starlight: Mix_LoadMUS failed for '%s': %s\n",
                SL_RESOURCE_COPYEFF_WAV, Mix_GetError());
    }

    if (g_startup_music && Mix_PlayMusic(g_startup_music, 0) != 0) {
        fprintf(stderr, "starlight: Mix_PlayMusic failed: %s\n", Mix_GetError());
    }
}

void sl_audio_play_copy_effect(void) {
    if (!g_audio_open || !g_copy_effect_music) return;
    (void)Mix_PlayMusic(g_copy_effect_music, 0);
}

void sl_audio_shutdown(void) {
    if (!g_audio_open) return;

    Mix_HaltMusic();
    if (g_startup_music) {
        Mix_FreeMusic(g_startup_music);
        g_startup_music = NULL;
    }
    if (g_copy_effect_music) {
        Mix_FreeMusic(g_copy_effect_music);
        g_copy_effect_music = NULL;
    }

    Mix_CloseAudio();
    g_audio_open = 0;
}
