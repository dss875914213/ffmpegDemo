#include "player.h"

Player::Player() :
	m_state(STOP)
{
}

Player::~Player()
{

}

int Player::Play(const char* pInputFile)
{
	m_demux = make_unique<FFDemux>(*this);
	m_video = make_unique<Video>();
	m_audio = make_unique<Audio>(*this);
	BOOL bRes = FALSE;
	// 播放器初始化
	bRes = PlayerInit(pInputFile);
	if (!bRes)
	{
		cout << "player init failed" << endl;
		Stop();  // 开始退出
	}

	m_demux->Open();		// 打开多路复用器，并创建线程用于解封装
	m_video->Open();		// 打开图像渲染
	m_audio->Open();		// 打开音频渲染

	SDL_Event event;
	// 主线程
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
				Stop();
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// 切换暂停
				Pause();
				break;
			case SDL_WINDOWEVENT:
				break;
			default:
				break;
			}
			break;
		case SDL_QUIT:
		case FF_QUIT_EVENT:
			Stop();
			break;

		default:
			break;
		}
	}
	return 0;
}

BOOL Player::IsStop()
{
	return m_state == STOP;
}

BOOL Player::IsPause()
{
	return m_state == PAUSE;
}

shared_ptr <FFDemux> Player::GetDemux()
{
	return m_demux;
}

shared_ptr <Audio> Player::GetAudio()
{
	return m_audio;
}

BOOL Player::PlayerInit(string pInputFile)
{
	// 失能停止请求
	m_state = RUN;

	// demux 初始化  video 初始化 audio 初始化
	m_demux->Init(pInputFile);
	m_video->Init(m_demux->GetPacketQueue(true), this);
	m_audio->Init(m_demux->GetPacketQueue(false));
	
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
	m_state = STOP;
	if (m_video)
	{
		m_video->Close();
		m_video = NULL;
	}

	if (m_audio)
	{
		m_audio->Close();
		m_audio = NULL;
	}

	if (m_demux)
	{
		m_demux->Close();
		m_demux = NULL;
	}
	return 0;
}

void Player::Stop()
{
	PlayerDeinit(); // 播放器反初始化
	avformat_network_deinit();	// 撤销 avformat_network_init 的初始化
	SDL_Quit(); // 退出 SDL
	exit(0);	// 退出系统
}
void Player::Pause()
{
	if (m_state == RUN)
	{
		m_state = PAUSE;
	}
	else if(m_state == PAUSE)
	{
		m_video->Pause();
		m_state = RUN;
	}
}
