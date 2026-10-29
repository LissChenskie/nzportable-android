#include "../../nzportable_def.h"
#include "sdl_local.h"
#ifdef PLATFORM_ANDROID
#include <SDL_mixer.h>
#endif

#define SDL_DMA_SAMPLES 32768
static SDL_AudioDeviceID audio_device;
static SDL_atomic_t audio_position;
#ifdef PLATFORM_ANDROID
static SDL_atomic_t audio_paused;
#endif

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int length)
{
	int bytes = shm->samples * (shm->samplebits / 8);
	unsigned int position = (unsigned int)SDL_AtomicGet(&audio_position);
	int offset = (position * (shm->samplebits / 8)) % bytes;
	int first = length < bytes - offset ? length : bytes - offset;
	(void)userdata;
#ifdef PLATFORM_ANDROID
	if (SDL_AtomicGet(&audio_paused)) return;
	// SDL_mixer already decoded the music into stream. Add the engine ring buffer.
	SDL_MixAudioFormat(stream, shm->buffer + offset, AUDIO_S16SYS, first, SDL_MIX_MAXVOLUME);
	if (first < length) SDL_MixAudioFormat(stream + first, shm->buffer, AUDIO_S16SYS, length - first, SDL_MIX_MAXVOLUME);
#else
	memcpy(stream, shm->buffer + offset, first);
	if (first < length) memcpy(stream + first, shm->buffer, length - first);
#endif
	SDL_AtomicSet(&audio_position, (position + length / (shm->samplebits / 8)) % shm->samples);
}

qboolean SNDDMA_Init(void)
{
#ifdef PLATFORM_ANDROID
	// Android SDL backends expose one default output device. Music and effects
	// must share it; opening a separate SDL device for each fails on OpenSL ES.
	if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Audio initialization: %s", Mix_GetError());
		return false;
	}
	int frequency, channels; Uint16 format;
	Mix_QuerySpec(&frequency, &format, &channels);
	shm = &sn;
	shm->splitbuffer = 0; shm->samplebits = 16; shm->speed = frequency;
	shm->channels = channels; shm->samples = SDL_DMA_SAMPLES; shm->samplepos = 0; shm->submission_chunk = 1;
	shm->buffer = calloc(SDL_DMA_SAMPLES * 2, 1);
	if (!shm->buffer) { Mix_CloseAudio(); return false; }
	SDL_AtomicSet(&audio_position, 0); SDL_AtomicSet(&audio_paused, 0);
	Mix_SetPostMix(AudioCallback, NULL);
	return true;
#else
	SDL_AudioSpec wanted, obtained;
	memset(&wanted, 0, sizeof(wanted));
	wanted.freq = 44100;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 2;
	wanted.samples = 1024;
	wanted.callback = AudioCallback;
	audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
	if (!audio_device) { Con_Printf("SDL audio disabled: %s\n", SDL_GetError()); return false; }
	shm = &sn;
	shm->splitbuffer = 0;
	shm->samplebits = 16;
	shm->speed = obtained.freq;
	shm->channels = obtained.channels;
	shm->samples = SDL_DMA_SAMPLES;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = calloc(SDL_DMA_SAMPLES * (shm->samplebits / 8), sizeof(*shm->buffer));
	if (!shm->buffer) { SDL_CloseAudioDevice(audio_device); audio_device = 0; return false; }
	SDL_PauseAudioDevice(audio_device, 0);
	return true;
#endif
}

int SNDDMA_GetDMAPos(void) { return SDL_AtomicGet(&audio_position); }
#ifdef PLATFORM_ANDROID
void SNDDMA_SetPaused(int paused) { SDL_AtomicSet(&audio_paused, paused); }
#endif
void SNDDMA_Submit(void) {}
void SNDDMA_Shutdown(void) {
#ifdef PLATFORM_ANDROID
	Mix_SetPostMix(NULL, NULL); Mix_CloseAudio();
#else
	if (audio_device) SDL_CloseAudioDevice(audio_device);
#endif
	free(shm ? shm->buffer : NULL); audio_device = 0;
}

