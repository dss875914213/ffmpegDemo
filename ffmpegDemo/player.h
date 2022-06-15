#pragma once
#include "config.h"
#include <iostream>
#include <windows.h>
#include <string>
#include "demux.h"
#include "video.h"
#include "audio.h"


using namespace std;

class FFDemux;
class Video;
class Audio;
class Player
{
public:
	Player();
	~Player();
	int PlayerRunning(const char* pInputFile);
	BOOL IsStop();
	BOOL IsPause();
public:
	BOOL PlayerInit(string pInputFile);
	BOOL PlayerDeinit();
	void DoExit();
	void StreamTogglePause();
	void TogglePause();
private:
	BOOL	m_stop;
	BOOL	m_pause;
	BOOL	m_step;

	FFDemux*	m_demux;
	Video*		m_video;
	Audio*		m_audio;
};



