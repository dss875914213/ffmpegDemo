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

void SDLAudioCallback(void* userdata, uint8_t* stream, int len)
{
	AVCodecContext* pCodecContext = (AVCodecContext*)userdata;
	int copyLen;
	int getSize;
	static uint8_t sAudioBuffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static uint32_t sAudioLen = 0;
	static uint32_t sTxIndex = 0;

	AVPacket* pPacket;
	int frameSize = 0;
	int returnSize = 0;
	int ret;
	while (len > 0)
	{
		if (sAudioDecodeFinished)
		{
			SDL_PauseAudio(1);
			cout << "pause audio callback" << endl;
			return;
		}
		if (sTxIndex >= sAudioLen)
		{
			pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
			// 1. 从队列中读出一包音频数据
			if (PacketQueuePop(&sAudioPacketQueue, pPacket, 1) == 0)
			{
				cout << "audio packet buffer empty..." << endl;
				continue;
			}

			// 2. 解码音频包
			getSize = AudioDecodeFrame(pCodecContext, pPacket, sAudioBuffer, sizeof(sAudioBuffer));
			if (getSize < 0)
			{
				sAudioLen = 1024;
				memset(sAudioBuffer, 0, sAudioLen);
				av_packet_unref(pPacket);
			}
			else if (getSize == 0)
				sAudioDecodeFinished = true;
			else
			{
				sAudioLen = getSize;
				av_packet_unref(pPacket);
			}
			sTxIndex = 0;
		}
		copyLen = sAudioLen - sTxIndex;
		if (copyLen > len)
			copyLen = len;
		memcpy(stream, (uint8_t*)sAudioBuffer + sTxIndex, copyLen);
		len -= copyLen;
		stream += copyLen;
		sTxIndex += copyLen;
	}
}

static uint32_t SDLTimeCbRefresh(uint32_t interval, void* opaque)
{
	SDL_Event sdlEvent;
	sdlEvent.type = SDL_USEREVENT_REFRESH;
	SDL_PushEvent(&sdlEvent);
	return interval;
}

// 将视频包解码得到视频帧，然后写入 picture 队列
int VideoThread(void* arg)
{
	AVCodecContext* pCodecContext = (AVCodecContext*)arg;
	AVFrame* pFrameRaw = NULL;
	AVFrame* pFrameYUV = NULL;
	AVPacket* pPacket = NULL;
	SwsContext* swsContext = NULL;
	int bufferSize;
	uint8_t* buffer = NULL;
	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_Thread* sdlThread;
	SDL_Event sdlEvent;

	int ret = 0;
	int result = -1;
	bool flush = false;
	pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
	// A1. 分配 AVFrame
	// A1.1 分配 AVFrame结构，并不分配 data buffer
	pFrameRaw = av_frame_alloc();
	if (pFrameRaw == NULL)
	{
		cout << "av_frame_alloc() for pFrameRaw failed" << endl;
		result = -1;
		goto EXIT0;
	}
	pFrameYUV = av_frame_alloc();
	if (pFrameYUV == NULL)
	{
		cout << "av_frame_alloc() for pFrameYUV failed" << endl;
		result = -1;
		goto EXIT1;
	}

	// A1.2 为AVFrame.*data[] 手工分配缓冲区，用于存储 sws_scale 中目的帧的视频数据
	bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecContext->width, pCodecContext->height, 1);
	buffer = (uint8_t*)av_malloc(bufferSize);
	if (buffer == NULL)
	{
		cout << "av_malloc() for buffer failed" << endl;
		result = -1;
		goto EXIT2;
	}

	ret = av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P, pCodecContext->width, pCodecContext->height, 1);
	if (ret < 0)
	{
		cout << "av_image_fill_arrays() failed " << ret << endl;
		result = -1;
		goto EXIT3;
	}

	swsContext = sws_getContext(pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt, pCodecContext->width, pCodecContext->height, AV_PIX_FMT_YUV420P,
		SWS_BICUBIC, NULL, NULL, NULL);
	if (swsContext == NULL)
	{
		cout << "sws_getContext() failed" << endl;
		result = -1;
		goto EXIT4;
	}

	// B1 创建SDL 窗口
	screen = SDL_CreateWindow("simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecContext->width, pCodecContext->height, SDL_WINDOW_OPENGL);
	if (screen == NULL)
	{
		cout << "SDL_CreateWindow() failed" << endl;
		result = -1;
		goto EXIT5;
	}

	// B2 创建SDL_Renderer
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	if (sdlRenderer == NULL)
	{
		cout << "SDL_CreateRenderer() failed " << SDL_GetError() << endl;
		result = -1;
		goto EXIT5;
	}

	// B3 创建 SDL_Texture
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecContext->width, pCodecContext->height);
	if (sdlTexture == NULL)
	{
		cout << "SDL_CreateTexture() failed: " << SDL_GetError() << endl;
		result = -1;
		goto EXIT5;
	}

	// B4. SDL_Rect 赋值
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = pCodecContext->width;
	sdlRect.h = pCodecContext->height;

	while (1)
	{
		if (sVideoDecodeFinished)
			break;
		if (!flush)
		{
			// A3. 从队列中读出一包视频数据
			if (PacketQueuePop(&sVideoPacketQueue, pPacket, 0) == 0)
			{
				cout << "video packet queue empty..." << endl;
				av_usleep(10000);
				continue;
			}
			// A4. 视频解码：packet ==> frame
			// A4.1 向解码器喂数据
			ret = avcodec_send_packet(pCodecContext, pPacket);
			if (ret != 0)
			{
				if (ret == AVERROR_EOF)
				{
					cout << "video avcodec_send_packet(): the decoder has been flushed" << endl;
					av_packet_unref(NULL);
				}
				else if (ret == AVERROR(EAGAIN))
					cout << "video avcodec_send_packet(): input is not accepted in the current state" << endl;
				else if (ret == AVERROR(EINVAL))
					cout << "video avcodec_send_packet(): codec not opened, it is an encoder, or requires flush" << endl;
				else if (ret == AVERROR(ENOMEM))
					cout << "video avcodec_send_packet() failed to add packet to interval queue, or similar" << endl;
				else
					cout << "video avcodec_send_packet(): legitimate decoding errors" << endl;
				result = -1;
				goto EXIT6;
			}
			if (pPacket->data == NULL)
			{
				cout << "flush video decoder" << endl;
				flush = true;
			}
			av_packet_unref(pPacket);
		}

		// A4.2 接收解码器输出的数据，此处只处理视频帧，每次接收一个 packet，将之解码得到一个 frame
		ret = avcodec_receive_frame(pCodecContext, pFrameRaw);
		if (ret != 0)
		{
			if (ret == AVERROR_EOF)
			{
				cout << "video avcodec_receive_frame() the decoder has been fully flushed" << endl;
				sVideoDecodeFinished = true;
			}
			else if (ret == AVERROR(EAGAIN))
			{
				cout << "video avcodec_receive_frame():output is not avvailable in this state - user must try to send new input" << endl;
				continue;
			}
			else if (ret == AVERROR(EINVAL))
				cout << "video avcodec_receive_frame():codec not opened, or it is an encoder" << endl;
			else
				cout << "video avcodec_receive_frame():legitimate decoding errors" << endl;
			result = -1;
			goto EXIT6;
		}

		// A5 图像转换
		sws_scale(swsContext, (const uint8_t* const*)pFrameRaw->data, pFrameRaw->linesize, 0, pCodecContext->height, pFrameYUV->data, pFrameYUV->linesize);

		// B5 使用新的 YUV 像素更新 SDL_Rect
		SDL_UpdateYUVTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);
		
		// B6 使用特定颜色清空当前渲染目标
		SDL_RenderClear(sdlRenderer);
		// B9. 使用部分图像数据更新当前渲染目标
		SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

		// B7. 执行渲染，更新屏幕显示
		SDL_RenderPresent(sdlRenderer);
		SDL_WaitEvent(&sdlEvent);
	}
EXIT6:
	if (pPacket != NULL)
		av_packet_unref(pPacket);
EXIT5:
	sws_freeContext(swsContext);
EXIT4:
	av_free(buffer);
EXIT3:
	av_frame_free(&pFrameYUV);
EXIT2:
	av_frame_free(&pFrameRaw);
EXIT1:
	avcodec_close(pCodecContext);
EXIT0:
	return result;
}
