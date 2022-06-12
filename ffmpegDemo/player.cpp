#include "player.h"
#include "frame.h"
#include "packet.h"
#include "demux.h"
#include "video.h"
#include "audio.h"

extern SDL_AudioDeviceID audioDevice;

// ����ֵ��������һ֡ pts ����ֵ����һ֡ pts +���ŵ�ʱ�䣩
double GetClock(PlayClock* playClock)
{
	if (*playClock->queueSerial != playClock->serial)
		return NAN;
	if (playClock->paused)
		return playClock->pts;
	else
	{
		double time = av_gettime_relative() / 1000000.0;

		// չ���ã�c->pts + (time-c->last_updated)
		// pts + ��֡��Ⱦ��ʱ��
		double ret = playClock->ptsDrift + time; 
		return ret;
	}
}

// ���� clock �Ĳ���
void SetClockAt(PlayClock* clock, double pts, int serial, double time)
{
	// ������Ⱦʱ��
	clock->pts = pts;
	// �����ϴθ���ʱ��
	clock->lastUpdated = time;
	// ��ǰ֡��ʾʱ����뵱ǰϵͳʱ��ʱ��Ĳ�ֵ
	clock->ptsDrift = clock->pts - time;
	// ���ò�������
	clock->serial = serial;
}

// ����ʱ��
void SetClock(PlayClock* clock, double pts, int serial)
{
	// time ��λ(ns)
	double time = av_gettime_relative() / 1000000.0; // av_gettime_relative ��ȡ��ĳ��δָ����������ĵ�ǰʱ�䣨��΢��Ϊ��λ��
	SetClockAt(clock, pts, serial, time);
}

int PlayerRunning(const char* pInputFile)
{
	PlayerStation* is = NULL;
	// ��������ʼ��
	is = PlayerInit(pInputFile);
	if (is == NULL)
	{
		cout << "player init failed" << endl;
		// ��ʼ�˳�
		DoExit(is);
	}

	// �򿪶�·������
	OpenDemux(is);
	// ��ͼ����Ⱦ
	OpenVideo(is);
	// ����Ƶ��Ⱦ
	OpenAudio(is);

	SDL_Event event;
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
				DoExit(is);
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// �л���ͣ
				TogglePause(is);
				break;
			case SDL_WINDOWEVENT:
				break;
			default:
				break;
			}
			break;
		case SDL_QUIT:
		case FF_QUIT_EVENT:
			DoExit(is);
			break;

		default:
			break;
		}
	}
	return 0;
}

static void SetClockSpeed(PlayClock* clock, double speed)
{
	SetClock(clock, GetClock(clock), clock->serial);
	clock->speed = speed;
}

// ��ʼ��ʱ��
static void InitClock(PlayClock* clock, int* queueSerial)
{
	// ���ò����ٶ�
	clock->speed = 1.0;
	// ���ò���ͣ
	clock->paused = 0;
	// ���ò�������
	clock->queueSerial = queueSerial;
	SetClock(clock, NAN, -1);
}

static void SyncPlayClockToSlave(PlayClock* playClock, PlayClock* slave)
{
	double clock = GetClock(playClock);
	double slaveClock = GetClock(slave);
	if (!isnan(slaveClock) && (isnan(clock) || fabs(clock - slaveClock) > AV_NOSYNC_THRESHOLD))
		SetClock(playClock, slaveClock, slave->serial);
}

// ��ʼ�˳�
static void DoExit(PlayerStation* is)
{
	if (is)
		PlayerDeinit(is); // ����������ʼ��
	if (is->sdlVideo.renderer)
		SDL_DestroyRenderer(is->sdlVideo.renderer); // ������Ⱦ��
	if (is->sdlVideo.window)
		SDL_DestroyWindow(is->sdlVideo.window); // ���ٴ���
	avformat_network_deinit();	// ���� avformat_network_init �ĳ�ʼ��
	SDL_Quit(); // �˳� SDL
	exit(0);	// �˳�ϵͳ
}

//��������ʼ��
static PlayerStation* PlayerInit(const char* pInputFile)
{
	PlayerStation* is;  // ������״̬������
	is = static_cast<PlayerStation*>(av_mallocz(sizeof(PlayerStation)));  // av_malloc zero �����ڴ棬�����ÿ��ڴ���Ϊ0
	if (!is)
	{
		cout << "[ERROR] Failed to av_mallocz" << endl;
		return NULL;
	}
	is->filename = av_strdup(pInputFile); // string duplicate �����ַ���
	if (is->filename == NULL)
		goto FAIL;
	// ��ʼ������Ƶδ�������
	if (FrameQueueInit(&is->videoFrameQueue, &is->videoPacketQueue, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0 ||
		FrameQueueInit(&is->audioFrameQueue, &is->audioPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto FAIL;

	// ��ʼ������Ƶ�������
	if (PacketQueueInit(&is->videoPacketQueue) < 0 || PacketQueueInit(&is->audioPacketQueue) < 0)
		goto FAIL;

	AVPacket flushPacket;
	flushPacket.data = NULL;
	// ��ˢ�����ݷ���δ���������
	PacketQueuePut(&is->videoPacketQueue, &flushPacket);
	PacketQueuePut(&is->audioPacketQueue, &flushPacket);

	// �������߳��ź���
	if (!(is->continueReadThread = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
	FAIL:
		// ����������ʼ��
		PlayerDeinit(is);
		return is;
	}

	// ��ʼ������Ƶʱ��
	InitClock(&is->videoPlayClock, &is->videoPacketQueue.serial);
	InitClock(&is->audioPlayClock, &is->audioPacketQueue.serial);

	// ʧ��ֹͣ����
	is->abortRequest = 0;
	// ��ʼ�� SDL, ��������Ƶ�Ͷ�ʱ������
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		exit(1);
	}

	return is;
}

// ����������ʼ��
static int PlayerDeinit(PlayerStation* is)
{
	// TODO ������֮�󣬰� ESC �����˳�����
	is->abortRequest = 1;
	// ֹͣ��Ƶ�ص�����
	SDL_PauseAudioDevice(audioDevice, 1);
	FrameQueueSignal(&is->videoFrameQueue);
	FrameQueueSignal(&is->audioFrameQueue);
	PacketQueueAbort(&is->videoPacketQueue);
	PacketQueueAbort(&is->audioPacketQueue);

	SDL_WaitThread(is->videoDecodeThreadID, NULL);
	SDL_WaitThread(is->videoPlayThreadID, NULL);
	SDL_WaitThread(is->audioDecodeThreadID, NULL);
	SDL_WaitThread(is->readThreadID, NULL);


	avformat_close_input(&is->pFormatContext);
	PacketQueueDestroy(&is->videoPacketQueue);
	PacketQueueDestroy(&is->audioPacketQueue);

	FrameQueueDestroy(&is->videoFrameQueue);
	FrameQueueDestroy(&is->audioFrameQueue);

	SDL_DestroyCond(is->continueReadThread);
	sws_freeContext(is->imgConvertContext);
	av_free(is->filename);
	if (is->sdlVideo.texture)
		SDL_DestroyTexture(is->sdlVideo.texture);
	av_free(is);
	return 0;
}

static void StreamTogglePause(PlayerStation* is)
{
	if (is->paused)
	{
		// ȡ����ͣ
		is->frameTimer += av_gettime_relative() / 1000000.0 - is->videoPlayClock.lastUpdated;
		SetClock(&is->videoPlayClock, GetClock(&is->videoPlayClock), is->videoPlayClock.serial);
	}
	is->paused = is->audioPlayClock.paused = is->videoPlayClock.paused = !is->paused;
}

// �л���ͣ
static void TogglePause(PlayerStation* is)
{
	StreamTogglePause(is);
	is->step = 0;
}


