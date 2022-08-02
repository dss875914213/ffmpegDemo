#include "player.h"

Player::Player():
	m_stop(FALSE),
	m_pause(FALSE),
	m_step(FALSE),
	m_demux(NULL),
	m_video(NULL),
	m_audio(NULL)
{
}

Player::~Player()
{
	
}

int Player::PlayerRunning(const char* pInputFile)
{
	SAFE_DELETE(m_demux);
	SAFE_DELETE(m_video);
	SAFE_DELETE(m_audio);

	m_demux = new FFDemux();
	m_video = new Video();
	m_audio = new Audio();
	BOOL bRes = FALSE;
	// ��������ʼ��
	bRes = PlayerInit(pInputFile);
	if (!bRes)
	{
		cout << "player init failed" << endl;
		DoExit();  // ��ʼ�˳�
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
				DoExit();
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// �л���ͣ
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

FFDemux* Player::GetDemux()
{
	return m_demux;
}

Audio* Player::GetAudio()
{
	return m_audio;
}

BOOL Player::PlayerInit(string pInputFile)
{
	// demux ��ʼ��  video ��ʼ�� audio ��ʼ��
	m_demux->Init(pInputFile, this);
	m_video->Init(m_demux->GetPacketQueue(true), this);
	m_audio->Init(m_demux->GetPacketQueue(false), this);

	// ʧ��ֹͣ����
	m_stop = FALSE;
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
	m_stop = TRUE;
	cout << "PlayerDeinit start" << endl;
	if (m_video)
	{
		m_video->Close();
		delete m_video;
		m_video = NULL;
	}

	cout << "video closed" << endl;

	if (m_audio)
	{
		m_audio->Close();
		delete m_audio;
		m_audio = NULL;
	}

	cout << "audio closed" << endl;

	
	if (m_demux)
	{
		m_demux->Close();
		delete m_demux;
		m_demux = NULL;
	}
	cout << "PlayerDeinit end" << endl;
	return 0;
}

void Player::DoExit()
{
	PlayerDeinit(); // ����������ʼ��
	avformat_network_deinit();	// ���� avformat_network_init �ĳ�ʼ��
	SDL_Quit(); // �˳� SDL
	exit(0);	// �˳�ϵͳ
}
void Player::TogglePause()
{
	if (m_pause)
		m_video->TogglePause();
	m_pause = !m_pause;
	m_step = FALSE;
}
