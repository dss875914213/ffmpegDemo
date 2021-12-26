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
	double pts; // ��ǰ֡����ʱ���
	double ptsDrift; // ��ǰ֡��ʾʱ����뵱ǰϵͳʱ��ʱ��Ĳ�ֵ
	double lastUpdated; // ��ǰʱ�ӣ����һ�θ���ʱ��
	double speed;		// ʱ���ٶȿ��ƣ����ڿ��Ʋ����ٶ�
	int serial;		// �������У�һ�� seek ����������һ���µĲ�������
	int paused;		// ��ͣ��־
	int* queueSerial; // ָ�� packet_serial
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
	int64_t pos;	// frame ��Ӧ packet �������ļ��еĵ�ַƫ��
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
	int rindex;		// ������
	int windex;		// д����
	int size;		// ��֡��
	int maxSize;	// �����пɴ洢���֡��
	int keepLast;
	int rindexShown;		// ��ǰ�Ƿ���֡����ʾ
	SDL_mutex* mutex;
	SDL_cond* cond;
	PacketQueue* packetQueue; // ��Ӧ�� packetQueue
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
	int audioHardwareBufferSize;	// SDL ��Ƶ��������С����λ�ֽڣ�
	uint8_t* pAudioFrame;		// ָ������ŵ�һ֡��Ƶ���ݣ�ָ������ݽ�������SDL��Ƶ������
	uint8_t* pAudioFrameRwr; // ��Ƶ�ز��������������
	unsigned int audioFrameSize; // �����ŵ�һ֡��Ƶ���ݴ�С
	unsigned int audioFrameRwrSize; // ���뵽����Ƶ������ pAudioFrameRwr ��С
	int audioCopyIndex;		// ��ǰ��Ƶ֡���ѿ��� SDL ��Ƶ��������λ������
	int audioWriteBufferSize; // ��ǰ��Ƶ֡����δ���� SDL ��Ƶ�������������� audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double audioClock;
	int audioClockSerial;

	int abortRequest;
	int paused;
	int step;

	SDL_cond* continueReadThread;
	SDL_Thread* readThreadID;  // demux �⸴���߳�
	SDL_Thread* videoPlayThreadID;
	SDL_Thread* videoDecodeThreadID;
	SDL_Thread* audioDecodeThreadID;
}PlayerStation;


int PlayerRunning(const char* pInputFile);
double GetClock(PlayClock* clock);
void SetClockAt(PlayClock* clock, double pts, int serial, double time);
void SetClock(PlayClock* clock, double pts, int serial);

