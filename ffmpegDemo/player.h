#pragma once

#include<iostream>
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/frame.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
}

#if defined(_WIN32)
#include "SDL/SDL.h"
#include "SDL/SDL_video.h"
#include "SDL/SDL_render.h"
#include "SDL/SDL_rect.h"
#include "SDL/SDL_mutex.h"
#endif

#define AV_SYNC_THRESHOLD_MIN 0.04
#define AV_SYNC_THRESHOLD_MAX 0.1
#define AV_SYNC_FRAMEUP_THRESHOLD 0.1
#define AV_NOSYNC_THRESHOLD 10.0

#define REFRESH_RATE 0.01

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_QUEUE_SIZE (15*1024*1024)
#define MIN_FRAMES 25

#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

#define FF_QUIT_EVENT (SDL_USEREVENT+2)

typedef struct 
{
	double pts; // 当前帧播放时间戳
	double ptsDrift; // 当前帧显示时间戳与当前系统时钟时间的差值
	double lastUpdated; // 当前时钟，最后一次更新时间
	double speed;		// 时钟速度控制，用于控制播放速度
	int serial;		// 播放序列，一个 seek 操作会启动一段新的播放序列
	int paused;		// 暂停标志
	int* queueSerial; // 指向 packet_serial
}PlayClock;

typedef struct
{
	int freq;
	int channels;
	int64_t channelLayout;
	enum AVSampleFormat fmt;
	int frameSize;
	int bytesPerSec;
}AudioParam;

typedef struct  
{
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* texture;
	SDL_Rect rect;
}SDLVideo;

typedef struct
{
	AVPacketList* firstPacket, * lastPacket;
	int numberPackets;
	int size;
	int64_t duration;
	int abortRequest;
	int serial;
	SDL_mutex* mutex;
	SDL_cond* cond;
}PacketQueue;


typedef struct
{
	AVFrame* frame;
	int serial;
	double pts;
	double duration;
	int64_t pos;	// frame 对应 packet 在输入文件中的地址偏移
	int width;
	int height;
	int format;
	AVRational sar;
	int uploaded;
	int flipV;
}Frame;

typedef struct
{
	Frame queue[FRAME_QUEUE_SIZE];
	int rindex;		// 读索引
	int windex;		// 写索引
	int size;		// 总帧数
	int maxSize;	// 队列中可存储最大帧数
	int keepLast;
	int rindexShown;		// 当前是否有帧在显示
	SDL_mutex* mutex;
	SDL_cond* cond;
	PacketQueue* packetQueue; // 对应的 packetQueue
}FrameQueue;

typedef struct
{
	char* filename;
	AVFormatContext* pFormatContext;
	AVStream* pAudioStream;
	AVStream* pVideoStream;
	AVCodecContext* pAudioCodecContext;
	AVCodecContext* pVideoCodecContext;
	int audioIndex;
	int videoIndex;
	SDLVideo sdlVideo;

	PlayClock audioPlayClock;
	PlayClock videoPlayClock;
	double frameTimer;

	PacketQueue audioPacketQueue;
	PacketQueue videoPacketQueue;
	FrameQueue audioFrameQueue;
	FrameQueue videoFrameQueue;

	SwsContext* imgConvertContext;
	SwrContext* audioSwrContext;
	AVFrame* pFrameYUV;

	AudioParam audioParamSource;
	AudioParam audioParamTarget;
	int audioHardwareBufferSize;	// SDL 音频缓冲区大小（单位字节）
	uint8_t* pAudioFrame;		// 指向待播放的一帧音频数据，指向的数据将被拷入SDL音频缓冲区
	uint8_t* pAudioFrameRwr; // 音频重采样的输出缓冲区
	unsigned int audioFrameSize; // 待播放的一帧音频数据大小
	unsigned int audioFrameRwrSize; // 申请到的音频缓冲区 pAudioFrameRwr 大小
	int audioCopyIndex;		// 当前音频帧中已拷入 SDL 音频缓冲区的位置索引
	int audioWriteBufferSize; // 当前音频帧中尚未拷入 SDL 音频缓冲区的数据量 audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double audioClock;
	int audioClockSerial;

	int abortRequest;
	int paused;
	int step;

	SDL_cond* continueReadThread;
	SDL_Thread* readThreadID;  // demux 解复用线程
	SDL_Thread* videoPlayThreadID;
	SDL_Thread* videoDecodeThreadID;
	SDL_Thread* audioDecodeThreadID;
}PlayerStation;


int PlayerRunning(const char* pInputFile);
double GetClock(PlayClock* clock);
void SetClockAt(PlayClock* clock, double pts, int serial, double time);
void SetClock(PlayClock* clock, double pts, int serial);

