#pragma once
#include "config.h"
#include <iostream>
#include <windows.h>
#include <string>
#include "demux.h"
#include "video.h"
#include "audio.h"
#include <memory>

using namespace std;
#define SAFE_DELETE(p) if(p){ delete p; p = NULL;}

class FFDemux;
class Video;
class Audio;

enum PlayerStates { RUN, STOP, PAUSE };
class Player
{
public:
	Player();
	~Player();
	Player(const Player&) = delete;
	int			Play(const char* pInputFile);
	void		Pause();
	void		Stop();

	BOOL		IsPause();
	BOOL		IsStop();
	shared_ptr<FFDemux> GetDemux();
	shared_ptr<Audio> GetAudio();
private:
	BOOL		PlayerInit(string pInputFile);
	BOOL		PlayerDeinit();
private:
	PlayerStates m_state;
	// 观察者模式，当 player 状态改变时，通知这些对象
	shared_ptr<FFDemux>		m_demux;
	shared_ptr<Video>		m_video;
	shared_ptr<Audio>		m_audio;
};



