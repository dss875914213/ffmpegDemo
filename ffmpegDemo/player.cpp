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
	// ��������ʼ��
	bRes = PlayerInit(pInputFile);
	if (!bRes)
	{
		cout << "player init failed" << endl;
		Stop();  // ��ʼ�˳�
	}

	m_demux->Open();		// �򿪶�·���������������߳����ڽ��װ
	m_video->Open();		// ��ͼ����Ⱦ
	m_audio->Open();		// ����Ƶ��Ⱦ

	SDL_Event event;
	// ���߳�
	while (1)
	{
		// �����¼�����
		SDL_PumpEvents();
		// �Ӷ�����ȡ��һ���¼�
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
		{
			av_usleep(100000); // ��ʱ 100ms
			SDL_PumpEvents();
		}

		switch (event.type)
		{
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				// ��ʼ�˳�
				Stop();
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// �л���ͣ
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
	// ʧ��ֹͣ����
	m_state = RUN;

	// demux ��ʼ��  video ��ʼ�� audio ��ʼ��
	m_demux->Init(pInputFile);
	m_video->Init(m_demux->GetPacketQueue(true), this);
	m_audio->Init(m_demux->GetPacketQueue(false));
	
	// ��ʼ�� SDL, ��������Ƶ�Ͷ�ʱ������
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
	PlayerDeinit(); // ����������ʼ��
	avformat_network_deinit();	// ���� avformat_network_init �ĳ�ʼ��
	SDL_Quit(); // �˳� SDL
	exit(0);	// �˳�ϵͳ
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
