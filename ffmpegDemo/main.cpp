#include <iostream>
#include "SDL/SDL.h"

using namespace std;
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
}

#define SDL_USEREVENT_REFRESH (SDL_USEREVENT+1)
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000
SDL_AudioDeviceID audioDevice;

typedef struct packetQueue
{
	AVPacketList* firstPacket;
	AVPacketList* lastPacket;
	int nbPackets;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
}packetQueue;


typedef struct AudioParams
{
	int freq;
	int channels;
	int64_t channelLayout;
	enum AVSampleFormat format;
	int frameSize;
	int bytesPerSecond;
}FF_AudioParams;

static FF_AudioParams sAudioParamSource;
static FF_AudioParams sAudioParamTarget;
static SwrContext* sAudioSwrContext;
static uint8_t* sResampleBuffer = NULL; // 重采样输出缓冲区
static int sResampleBufferLen = 0; // 重采样输出缓冲区长度
static bool sAudioDecodeFinished = false; // 音频解码完毕
static bool sVideoDecodeFinished = false; // 视频解码完毕
static packetQueue sAudioPacketQueue;
static packetQueue sVideoPacketQueue;

void PacketQueueInit(packetQueue* q)
{
	memset(q, 0, sizeof(packetQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int PacketQueueNumber(packetQueue* q)
{
	return q->nbPackets;
}

// 写队列尾部
int PacketQueuePush(packetQueue* q, AVPacket* packet)
{
	AVPacketList* packetList;
	if ((packet != NULL) && (packet->data != NULL) && (av_packet_make_refcounted(packet) < 0))
	{
		cout << "[packet] is not reference counted" << endl;
		return -1;
	}
	packetList = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!packetList)
		return -1;
	packetList->pkt = *packet;
	packetList->next = NULL;
	SDL_LockMutex(q->mutex);
	if (!q->lastPacket)
		q->firstPacket = packetList;
	else
		q->lastPacket->next = packetList;
	q->lastPacket = packetList;
	q->nbPackets++;
	q->size += packetList->pkt.size;
	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return 0;
}

// 读队列头部
int PacketQueuePop(packetQueue* q, AVPacket* packet, int block)
{
	AVPacketList* pPacketNode;
	int ret;
	SDL_LockMutex(q->mutex);
	while (1)
	{
		pPacketNode = q->firstPacket;
		if (pPacketNode)
		{
			q->firstPacket = pPacketNode->next;
			if (!q->firstPacket)
				q->lastPacket = NULL;
			q->nbPackets--;
			q->size -= pPacketNode->pkt.size;
			*packet = pPacketNode->pkt;
			av_free(pPacketNode);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
			SDL_CondWait(q->cond, q->mutex);
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

int AudioDecodeFrame(AVCodecContext* pCodecContext, AVPacket* pPacket, uint8_t* audioBuffer, int bufferSize)
{
	AVFrame* pFrame = av_frame_alloc();
	int frameSize = 0;
	int result = 0;
	int ret = 0;
	int nbSamples = 0;
	uint8_t* pCpBuffer = NULL;
	int cpLen = 0;
	bool needNew = false;
	while (1)
	{
		needNew = false;

		// 1. 接收解码器输出的数据，每次接收一个 frame
		ret = avcodec_receive_frame(pCodecContext, pFrame);
		if (ret != 0)
		{
			if (ret == AVERROR_EOF)
			{
				cout << "audio avcodec_receive_frame(): the decoder has been fully flushed" << endl;
				result = 0;
				goto EXIT;
			}
			else if (ret == AVERROR(EAGAIN))
				needNew = true;
			else if (ret == AVERROR(EINVAL))
			{
				cout << "audio avcodec_receive_frame(): legitimate decoding errors" << endl;
				result = -1;
				goto EXIT;
			}
		}
		else
		{
			if (pFrame->format != sAudioParamSource.format ||
				pFrame->channel_layout != sAudioParamSource.channelLayout ||
				pFrame->sample_rate != sAudioParamSource.freq)
			{
				swr_free(&sAudioSwrContext);
				sAudioSwrContext = swr_alloc_set_opts(NULL, sAudioParamTarget.channelLayout, sAudioParamTarget.format,
					sAudioParamTarget.freq, pFrame->channel_layout, (AVSampleFormat)pFrame->format, pFrame->sample_rate, 0, NULL);
				if (sAudioSwrContext == NULL || swr_init(sAudioSwrContext) < 0)
				{
					cout << "Can't create sample rate converter for conversion of " << pFrame->sample_rate << " Hz " << av_get_sample_fmt_name((AVSampleFormat)pFrame->format)
						<< " " << pFrame->channels << " channels to " << sAudioParamTarget.freq << " Hz " << av_get_sample_fmt_name(sAudioParamTarget.format) << " " <<
						sAudioParamTarget.channels << " channels!" << endl;
					swr_free(&sAudioSwrContext);
					return -1;
				}
				sAudioParamSource.channelLayout = pFrame->channel_layout;
				sAudioParamSource.channels = pFrame->channels;
				sAudioParamSource.freq = pFrame->sample_rate;
				sAudioParamSource.format = (AVSampleFormat)pFrame->format;
			}

			if (sAudioSwrContext) //重采样
			{
				const uint8_t** in = (const uint8_t**)pFrame->nb_samples;
				uint8_t** out = &sResampleBuffer;
				int outCount = (int64_t)pFrame->nb_samples * sAudioParamTarget.freq / pFrame->sample_rate + 256;
				int outSize = av_samples_get_buffer_size(NULL, sAudioParamTarget.channels, outCount, sAudioParamTarget.format, 0);
				if (outSize < 0)
				{
					cout << "av_samples_get_buffer_size() failed" << endl;
					return -1;
				}
				if (sResampleBuffer == NULL)
					av_fast_malloc(&sResampleBuffer, (unsigned int*)&sResampleBufferLen, outSize);
				if (sResampleBuffer == NULL)
					return AVERROR(ENOMEM);
				nbSamples = swr_convert(sAudioSwrContext, out, outCount, in, pFrame->nb_samples);
				if (nbSamples < 0)
				{
					cout << "swr_convert() failed" << endl;
					return -1;
				}
				if (nbSamples == outCount)
				{
					cout << "audio buffer is probably too small" << endl;
					if (swr_init(sAudioSwrContext) < 0)
						swr_free(&sAudioSwrContext);
				}
				pCpBuffer = sResampleBuffer;
				cpLen = nbSamples * sAudioParamTarget.channels * av_get_bytes_per_sample(sAudioParamTarget.format);
			}
			else
			{
				frameSize = av_samples_get_buffer_size(NULL, pCodecContext->channels, pFrame->nb_samples, pCodecContext->sample_fmt, 1);
				cout << "frame size " << frameSize << " , buffer size " << bufferSize << endl;
				pCpBuffer = pFrame->data[0];
				cpLen = frameSize;
			}
			memcpy(audioBuffer, pCpBuffer, cpLen);
			result = cpLen;
			goto EXIT;
		}
		// 2 向解码器喂数据
		if (needNew)
		{
			ret = avcodec_send_packet(pCodecContext, pPacket);
			if (ret != 0)
			{
				cout << "avcodec_send_packet() failed " << ret << endl;
				result = -1;
				goto EXIT;
			}
		}
	}

EXIT:
	av_frame_unref(pFrame);
	return result;
}

