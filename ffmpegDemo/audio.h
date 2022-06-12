#pragma once
#include "player.h"

static void SDLAudioCallback(void* opaque, Uint8* stream, int len);
int OpenAudio(PlayerStation* is);

