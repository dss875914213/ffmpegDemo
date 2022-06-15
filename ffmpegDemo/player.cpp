#include "player.h"

Player::Player():
	m_stop(FALSE),
	m_pause(FALSE),
	m_step(FALSE)
{
	m_demux = new FFDemux();
	m_video = new Video();
	m_audio = new Audio();
}

Player::~Player()
{
	delete m_demux;
	delete m_video;
	delete m_audio;
}

int Player::PlayerRunning(const char* pInputFile)
{
	BOOL bRes = FALSE;
	// 播放器初始化
	bRes = PlayerInit(pInputFile);
	if (!bRes)
	{
		cout << "player init failed" << endl;
		// 开始退出
		DoExit();
	}
	
	m_demux->Open();		// 打开多路复用器，并创建线程用于解封装
	m_video->Open();		// 打开图像渲染
	m_audio->Open();		// 打开音频渲染

	SDL_Event event;
	while (1)
	{
		// 更新事件队列
		SDL_PumpEvents();
		// 从队列中取出一个事件
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
		{
			av_usleep(100000); // 延时 100ms
			SDL_PumpEvents();
		}

		switch (event.type)
		{
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				// 开始退出
				DoExit();
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// 切换暂停
				TogglePause();
				break;
			case SDL_WINDOWEVENT:
				break;
			default:
				break;
			}
			break;
		case SDL_QUIT:
		case FF_QUIT_EVENT:
			DoExit();
			break;

		default:
			break;
		}
	}
	return 0;
}

BOOL Player::IsStop()
{
	return m_stop;
}

BOOL Player::IsPause()
{
	return m_pause;
}

BOOL Player::PlayerInit(string pInputFile)
{
	// demux 初始化  video 初始化 audio 初始化
	m_demux->Init(pInputFile, this);
	m_video->Init(m_demux->GetVideoPacketQueue(), this);
	m_audio->Init(m_demux->GetAudioPacketQueue(), this);

	// 失能停止请求
	m_stop = FALSE;
	// 初始化 SDL, 开启音视频和定时器能力
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		exit(1);
	}

	return TRUE;
}

int Player::PlayerDeinit()
{
	m_stop = TRUE;
	m_audio->Close();
	m_video->Close();
	m_demux->Close();
	return 0;
}

void Player::DoExit()
{
	PlayerDeinit(); // 播放器反初始化
	m_video->Destroy();
	avformat_network_deinit();	// 撤销 avformat_network_init 的初始化
	SDL_Quit(); // 退出 SDL
	exit(0);	// 退出系统
}

void Player::StreamTogglePause()
{
	if (m_pause)
		m_video->TogglePause();
	m_pause = !m_pause;
}

void Player::TogglePause()
{
	StreamTogglePause();
	m_step = FALSE;
}
