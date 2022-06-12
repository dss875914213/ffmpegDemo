#include "player.h"
#include "frame.h"
#include "packet.h"
#include "demux.h"
#include "video.h"
#include "audio.h"

extern SDL_AudioDeviceID audioDevice;

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

		// 展开得：c->pts + (time-c->last_updated)
		// pts + 该帧渲染的时间
		double ret = playClock->ptsDrift + time; 
		return ret;
	}
}

// 设置 clock 的参数
void SetClockAt(PlayClock* clock, double pts, int serial, double time)
{
	// 设置渲染时间
	clock->pts = pts;
	// 设置上次更新时间
	clock->lastUpdated = time;
	// 当前帧显示时间戳与当前系统时钟时间的差值
	clock->ptsDrift = clock->pts - time;
	// 设置播放序列
	clock->serial = serial;
}

// 设置时钟
void SetClock(PlayClock* clock, double pts, int serial)
{
	// time 单位(ns)
	double time = av_gettime_relative() / 1000000.0; // av_gettime_relative 获取自某个未指定起点以来的当前时间（以微秒为单位）
	SetClockAt(clock, pts, serial, time);
}

int PlayerRunning(const char* pInputFile)
{
	PlayerStation* is = NULL;
	// 播放器初始化
	is = PlayerInit(pInputFile);
	if (is == NULL)
	{
		cout << "player init failed" << endl;
		// 开始退出
		DoExit(is);
	}

	// 打开多路复用器
	OpenDemux(is);
	// 打开图像渲染
	OpenVideo(is);
	// 打开音频渲染
	OpenAudio(is);

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
				DoExit(is);
				break;
			}
			switch (event.key.keysym.sym)
			{
			case SDLK_SPACE:
				// 切换暂停
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

// 初始化时钟
static void InitClock(PlayClock* clock, int* queueSerial)
{
	// 设置播放速度
	clock->speed = 1.0;
	// 设置不暂停
	clock->paused = 0;
	// 设置播放序列
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

// 开始退出
static void DoExit(PlayerStation* is)
{
	if (is)
		PlayerDeinit(is); // 播放器反初始化
	if (is->sdlVideo.renderer)
		SDL_DestroyRenderer(is->sdlVideo.renderer); // 销毁渲染器
	if (is->sdlVideo.window)
		SDL_DestroyWindow(is->sdlVideo.window); // 销毁窗口
	avformat_network_deinit();	// 撤销 avformat_network_init 的初始化
	SDL_Quit(); // 退出 SDL
	exit(0);	// 退出系统
}

//播放器初始化
static PlayerStation* PlayerInit(const char* pInputFile)
{
	PlayerStation* is;  // 播放器状态上下文
	is = static_cast<PlayerStation*>(av_mallocz(sizeof(PlayerStation)));  // av_malloc zero 分配内存，并将该块内存置为0
	if (!is)
	{
		cout << "[ERROR] Failed to av_mallocz" << endl;
		return NULL;
	}
	is->filename = av_strdup(pInputFile); // string duplicate 拷贝字符串
	if (is->filename == NULL)
		goto FAIL;
	// 初始化音视频未编码队列
	if (FrameQueueInit(&is->videoFrameQueue, &is->videoPacketQueue, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0 ||
		FrameQueueInit(&is->audioFrameQueue, &is->audioPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0)
		goto FAIL;

	// 初始化音视频编码队列
	if (PacketQueueInit(&is->videoPacketQueue) < 0 || PacketQueueInit(&is->audioPacketQueue) < 0)
		goto FAIL;

	AVPacket flushPacket;
	flushPacket.data = NULL;
	// 将刷新数据放入未解码队列中
	PacketQueuePut(&is->videoPacketQueue, &flushPacket);
	PacketQueuePut(&is->audioPacketQueue, &flushPacket);

	// 创建读线程信号量
	if (!(is->continueReadThread = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
	FAIL:
		// 播放器反初始化
		PlayerDeinit(is);
		return is;
	}

	// 初始化音视频时钟
	InitClock(&is->videoPlayClock, &is->videoPacketQueue.serial);
	InitClock(&is->audioPlayClock, &is->audioPacketQueue.serial);

	// 失能停止请求
	is->abortRequest = 0;
	// 初始化 SDL, 开启音视频和定时器能力
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
		exit(1);
	}

	return is;
}

// 播放器反初始化
static int PlayerDeinit(PlayerStation* is)
{
	// TODO 播放完之后，按 ESC 不能退出程序
	is->abortRequest = 1;
	// 停止音频回调函数
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
		// 取消暂停
		is->frameTimer += av_gettime_relative() / 1000000.0 - is->videoPlayClock.lastUpdated;
		SetClock(&is->videoPlayClock, GetClock(&is->videoPlayClock), is->videoPlayClock.serial);
	}
	is->paused = is->audioPlayClock.paused = is->videoPlayClock.paused = !is->paused;
}

// 切换暂停
static void TogglePause(PlayerStation* is)
{
	StreamTogglePause(is);
	is->step = 0;
}


