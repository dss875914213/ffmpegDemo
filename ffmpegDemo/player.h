#pragma once
#include "config.h"
#include <iostream>
#include <windows.h>
#include <string>
#include "demux.h"
#include "video.h"
#include "audio.h"

using namespace std;
#define SAFE_DELETE(p) if(p){ delete p; p = NULL;}

class FFDemux;
class Video;
class Audio;
class Player
{
public:
	Player();
	~Player();
	int			Play(const char* pInputFile);
	void		Pause();
	void		Stop();

	BOOL		IsPause();
	BOOL		IsStop();
	FFDemux*	GetDemux();
	Audio*		GetAudio();
private:
	BOOL		PlayerInit(string pInputFile);
	BOOL		PlayerDeinit();
private:
	BOOL		m_stop;
	BOOL		m_pause;

	FFDemux*	m_demux;
	Video*		m_video;
	Audio*		m_audio;
};



