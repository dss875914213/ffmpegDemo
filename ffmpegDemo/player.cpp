#include <iostream>
#include "player.h"
#include "frame.h"
#include "packet.h"
#include "demux.h"
#include "video.h"
#include "audio.h"

using namespace std;

static PlayerStation* PlayerInit(const char* pInputFile);
static int PlayerDeinit(PlayerStation* is);

// 返回值：返回上一帧 pts 更新值（上一帧 pts +流逝的时间）
double GetClock(PlayClock* playClock)
{
	if (*playClock->queueSerial != playClock->serial)
		return NAN;
	if (playClock->paused)
		return playClock->pts;
	else
	{
		double time = av_gettime_relative() / 1000000.0;
		double ret = playClock->ptsDrift + time; // 展开得：c->pts + (time-c->last_updated)
		return ret;
	}
}

void SetClockAt(PlayClock* clock, double pts, int serial, double time)
{
	clock->pts = pts;
	clock->lastUpdated = time;
	clock->ptsDrift = clock->pts - time;
	clock->serial = serial;
}

void SetClock(PlayClock* clock, double pts, int serial)
{
	double time = av_gettime_relative() / 1000000.0;
	SetClockAt(clock, pts, serial, time);
}

static void SetClockSpeed(PlayClock* clock, double speed)
{
	SetClock(clock, GetClock(clock), clock->serial);
	clock->speed = speed;
}

void InitClock(PlayClock* clock, int* queueSerial)
{
	clock->speed = 1.0;
	clock->paused = 0;
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

static void DoExit(PlayerStation* is)
{
	if (is)
		PlayerDeinit(is);
	if (is->sdlVideo.renderer)
		SDL_DestroyRenderer(is->sdlVideo.renderer);
	if (is->sdlVideo.window)
		SDL_DestroyWindow(is->sdlVideo.window);
	avformat_network_deinit();
	SDL_Quit();
	exit(0);
}

static PlayerStation* PlayerInit(const char* pInputFile)
{
	PlayerStation* is;
	is = static_cast<PlayerStation*>(av_mallocz(sizeof(PlayerStation)));
	if (!is)
		return NULL;
	is->filename = av_strdup(pInputFile);
	if (is->filename == NULL)
		goto FAIL;
	if (FrameQueueInit(&is->videoFrameQueue, &is->videoPacketQueue, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0 ||
		FrameQueueInit(&is->audioFrameQueue, &is->audioPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto FAIL;

	if (PacketQueueInit(&is->videoPacketQueue) < 0 ||
		PacketQueueInit(&is->audioPacketQueue) < 0)
		goto FAIL;
	AVPacket flushPacket;
	flushPacket.data = NULL;
	PacketQueuePut(&is->videoPacketQueue, &flushPacket);
	PacketQueuePut(&is->audioPacketQueue, &flushPacket);
	if (!(is->continueReadThread = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
	FAIL:
		PlayerDeinit(is);
		// TODO 怎么循环了
		goto FAIL;
	}
	InitClock(&is->videoPlayClock, &is->videoPacketQueue.serial);
	InitClock(&is->audioPlayClock, &is->audioPacketQueue.serial);

	is->abortRequest = 0;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		exit(1);
	}

	return is;
}

extern SDL_AudioDeviceID audioDevice;
static int PlayerDeinit(PlayerStation* is)
{
	is->abortRequest = 1;
	SDL_PauseAudioDevice(audioDevice, 1);
	SDL_WaitThread(is->readThreadID, NULL);
	FrameQueueSignal(&is->videoFrameQueue);
	FrameQueueSignal(&is->audioFrameQueue);
	PacketQueueAbort(&is->videoPacketQueue);
	PacketQueueAbort(&is->audioPacketQueue);

	SDL_WaitThread(is->videoDecodeThreadID, NULL);
	SDL_WaitThread(is->videoPlayThreadID, NULL);
	SDL_WaitThread(is->audioDecodeThreadID, NULL);

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
		is->frameTimer += av_gettime_relative() / 1000000.0 - is->videoPlayClock.lastUpdated;
		SetClock(&is->videoPlayClock, GetClock(&is->videoPlayClock), is->videoPlayClock.serial);
	}
	is->paused = is->audioPlayClock.paused = is->videoPlayClock.paused = !is->paused;
}

static void TogglePause(PlayerStation* is)
{
	StreamTogglePause(is);
	is->step = 0;
}

int PlayerRunning(const char* pInputFile)
{
	PlayerStation* is = NULL;
	is = PlayerInit(pInputFile);
	if (is == NULL)
	{
		cout << "player init failed" << endl;
		DoExit(is);
	}
	OpenDemux(is);
	OpenVideo(is);
	OpenAudio(is);

	SDL_Event event;
	while (1)
	{
		SDL_PumpEvents();
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
		{
			av_usleep(100000);
			SDL_PumpEvents();
		}

		switch (event.type)
		{
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				DoExit(is);
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				TogglePause(is);
				break;
			case SDL_WINDOWEVENT:
				break;
			default:
				break;
			}
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
