#pragma once

// ���� ffmpeg ͷ�ļ�
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

// ���� SDL ͷ�ļ�
#include "SDL/SDL.h"
#include "SDL/SDL_video.h"
#include "SDL/SDL_render.h"
#include "SDL/SDL_rect.h"
#include "SDL/SDL_mutex.h"

// ��������
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
	double pts;				// ��ǰ֡��Ⱦʱ���
	double ptsDrift;		// ��ǰ֡��ʾʱ����뵱ǰϵͳʱ��ʱ��Ĳ�ֵ
	double lastUpdated;		// ��ǰʱ�ӣ����һ�θ���ʱ��
	double speed;			// ʱ���ٶȿ��ƣ����ڿ��Ʋ����ٶ�
	int serial;				// �������У�һ�� seek ����������һ���µĲ�������
	int paused;				// ��ͣ��־
	int* queueSerial;		// ָ�� packet_serial
}PlayClock;

typedef struct
{
	int freq;					// ����Ƶ��
	int channels;				// ͨ����
	int64_t channelLayout;		// ͨ������
	enum AVSampleFormat fmt;	// ��Ƶ��ʽ
	int frameSize;				// ��Ƶ֡�ĳ��� = ͨ���� * ÿ��������ռ�ֽ���
	int bytesPerSec;			// ÿ����ֽ��� = ͨ���� * ÿ��������ռ�ֽ��� * ÿ�������
}AudioParam;

typedef struct
{
	SDL_Window* window;			// ����
	SDL_Renderer* renderer;		// ��Ⱦ
	SDL_Texture* texture;		// ����
	SDL_Rect rect;				// ���ھ�������
}SDLVideo;

typedef struct
{
	AVPacketList* firstPacket;	// ��һ֡δ�������ݣ��Ӵ˴�ȡ���ݽ���
	AVPacketList* lastPacket;	// ���һ֡δ�������ݣ����»�ȡ�����ݷ����֡����
	int numberPackets;			// δ��������֡��
	int size;					// 
	int64_t duration;			// ��������ʱ��
	int abortRequest;			// ֹͣ����
	int serial;					// ��������
	SDL_mutex* mutex;			// ������
	SDL_cond* cond;				// ��������
}PacketQueue;

typedef struct
{
	AVFrame* frame;		// ���������
	int serial;
	double pts;			// ��Ⱦʱ��
	double duration;	// ��Ⱦ����ʱ��
	int64_t pos;		// frame ��Ӧ packet �������ļ��еĵ�ַƫ��
	int width;			// ���
	int height;			// �߶�
	int format;			// ��ʽ
	AVRational sar;		// �������ݱ� Sample aspect ratio 
	int flipV;			// ��ֱ����
}Frame;

typedef struct
{
	Frame queue[FRAME_QUEUE_SIZE];	// �������ݶ���
	int rindex;						// ������
	int windex;						// д����
	int maxSize;					// �����пɴ洢�����������֡��
	int keepLast;					// �Ƿ񱣴���һ֡����
	int rindexShown;				// ��ǰ�Ƿ���֡����ʾ
	int size;						// ��֡��
	SDL_mutex* mutex;				// ������
	SDL_cond* cond;					// ��������
	PacketQueue* packetQueue;		// ��Ӧ�� packetQueue δ�������ݶ���
}FrameQueue;

typedef struct
{
	char* filename;						// �ļ���
	AVFormatContext* pFormatContext;	// ��ý�����������
	AVStream* pAudioStream;				// ��Ƶ��
	AVStream* pVideoStream;				// ��Ƶ��
	AVCodecContext* pAudioCodecContext;	// ��Ƶ������������
	AVCodecContext* pVideoCodecContext;	// ��Ƶ������������
	int audioIndex;						// ��Ƶ������
	int videoIndex;						// ��Ƶ������
	SDLVideo sdlVideo;

	PlayClock audioPlayClock;			// ��Ƶ����ʱ��
	PlayClock videoPlayClock;			// ��Ƶ����ʱ��
	double frameTimer;					// ��ǰ֡��ʼ���ŵ�ʱ��

	PacketQueue audioPacketQueue;		// ��Ƶδ����֡����
	PacketQueue videoPacketQueue;		// ��Ƶδ����֡����
	FrameQueue audioFrameQueue;			// ��Ƶ����֡����
	FrameQueue videoFrameQueue;			// ��Ƶ����֡����

	SwsContext* imgConvertContext;		// ��Ƶת��������
	SwrContext* audioSwrContext;		// ��Ƶ��ʽת��������
	AVFrame* pFrameYUV;					// ת����ʽ�����Ƶ����

	AudioParam audioParamSource;		// Դ��Ƶ����(�������ݵĲ���)
	AudioParam audioParamTarget;		// Ŀ����Ƶ����
	int audioHardwareBufferSize;		// SDL ��Ƶ��������С����λ�ֽڣ�
	uint8_t* pAudioFrame;				// ָ������ŵ�һ֡��Ƶ���ݣ�ָ������ݽ�������SDL��Ƶ������
	uint8_t* pAudioFrameRwr;			// ��Ƶ�ز��������������
	unsigned int audioFrameSize;		// �����ŵ�һ֡��Ƶ���ݴ�С
	unsigned int audioFrameRwrSize;		// ���뵽����Ƶ������ pAudioFrameRwr ��С
	int audioCopyIndex;					// ��ǰ��Ƶ֡���ѿ��� SDL ��Ƶ��������λ������
	int audioWriteBufferSize;			// ��ǰ��Ƶ֡����δ���� SDL ��Ƶ�������������� audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double audioClock;					// ��Ƶʱ��(��ǰ֡���Ž�����ʱ�䣿)
	int audioClockSerial;

	int abortRequest;					// ֹͣ����
	int paused;							// ��ͣ
	int step;							// ����

	SDL_cond* continueReadThread;		// ���߳��ź���


	SDL_Thread* readThreadID;			// demux �⸴���߳�
	SDL_Thread* videoPlayThreadID;		// ��Ƶ��Ⱦ�߳�
	SDL_Thread* videoDecodeThreadID;	// ��Ƶ�����߳�
	SDL_Thread* audioDecodeThreadID;	// ��Ƶ�����߳�
}PlayerStation;