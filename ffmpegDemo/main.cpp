#include <iostream>
#include "SDL/SDL.h"
using namespace std;
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 19200
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
	enum AVSampleFormat fmt;
	int frameSize;
	int bytesPerSec;
}FF_AudioParams;

static packetQueue sAudioPacketQueue;
static FF_AudioParams sAudioParamSource;
static FF_AudioParams sAudioParamTarget;
static SwrContext* sAudioSwrContext;
static uint8_t* sResampleBuffer = NULL; // 重采样输出缓冲区
static int sResampleBufferLen = 0; // 重采样输出缓冲区长度

static bool sInputFinished = false; // 文件读取完毕
static bool sDecodeFinished = false; // 解码完毕

void PacketQueueInit(packetQueue* q)
{
	memset(q, 0, sizeof(packetQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

// 写队列尾部
int PacketQueuePush(packetQueue* q, AVPacket* packet)
{
	AVPacketList* packetList;
	if (av_packet_make_refcounted(packet) < 0)
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
	if (!q->lastPacket) // 队列为空
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
int PacketQueuePop(packetQueue* q, AVPacket* pkt, int block)
{
	AVPacketList* pPacketNode;
	int ret;
	SDL_LockMutex(q->mutex);
	while (1)
	{
		pPacketNode = q->firstPacket;
		if (pPacketNode) // 队列非空，取出头部数据
		{
			q->firstPacket = pPacketNode->next;
			if (!q->firstPacket)
				q->lastPacket = NULL;
			q->nbPackets--;
			q->size -= pPacketNode->pkt.size;
			*pkt = pPacketNode->pkt;
			av_free(pPacketNode);
			ret = 1;
			break;
		}
		else if (sInputFinished) // 文件已经处理完
		{
			ret = 0;
			pkt = NULL;
			break;
		}
		else if (!block) // 队列空且无阻塞标志，则立即退出
		{
			ret = 0;
			pkt = NULL;
			break;
		}
		else    // 等待信号量
			SDL_CondWait(q->cond, q->mutex);
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

int AudioDecodeFrame(AVCodecContext* pCodecContex, AVPacket* pPacket, uint8_t* audioBuffer, int bufferSize)
{
	AVFrame* pFrame = av_frame_alloc();
	int frameSize = 0;
	int result = 0;
	int ret = 0;
	int nbSamples = 0; // 重采样输出样本数
	uint8_t* pCpBuffer = NULL;
	int cpLen = 0;
	bool needNew = false;

	while (1)
	{
		needNew = false;
		// 接收解码器输出的数据，每次接收一个 frame
		ret = avcodec_receive_frame(pCodecContex, pFrame);
		if (ret != 0)
		{
			switch (ret)
			{
			case AVERROR_EOF:
				cout << "audio avcodec_receive_frame(): the decoder has been fully flushed" << endl;
				result = 0;
				goto EXIT;
			case AVERROR(EAGAIN):
				needNew = true;
				break;
			case AVERROR(EINVAL):
				cout << "audio avcodec_receive_frame(): codec not opened, or it is an encoder" << endl;
				result = -1;
				goto EXIT;
			default:
				cout << "audio avcodec_receive_frame(): legitimate decoding errors" << endl;
				result = -1;
				goto EXIT;
			}
		}
		else
		{
			// sAudioParamTarget 是 SDL 可接受的音频格式
			if (pFrame->format != sAudioParamSource.fmt ||
				pFrame->channel_layout != sAudioParamSource.channelLayout ||
				pFrame->sample_rate != sAudioParamSource.freq)
			{
				swr_free(&sAudioSwrContext);
				sAudioSwrContext = swr_alloc_set_opts(NULL,
					sAudioParamTarget.channelLayout, sAudioParamTarget.fmt, sAudioParamTarget.freq,
					pFrame->channel_layout, (AVSampleFormat)pFrame->format, pFrame->sample_rate, 0, NULL);
				if (sAudioSwrContext == NULL || swr_init(sAudioSwrContext) < 0)
				{
					cout << "Can't create sample rate converter for conversion of " << pFrame->sample_rate << " Hz " << av_get_sample_fmt_name((AVSampleFormat)pFrame->format)
						<< " " << pFrame->channels << " channels to " << sAudioParamTarget.freq << " Hz" << av_get_sample_fmt_name(sAudioParamTarget.fmt) << " "
						<< sAudioParamTarget.channels << " channels!" << endl;
					swr_free(&sAudioSwrContext);
					return -1;
				}
				sAudioParamSource.channelLayout = pFrame->channel_layout;
				sAudioParamSource.channels = pFrame->channels;
				sAudioParamSource.freq = pFrame->sample_rate;
				sAudioParamSource.fmt = (AVSampleFormat)pFrame->format;
			}
			if (sAudioSwrContext) // 重采样
			{
				const uint8_t** in = (const uint8_t**)pFrame->extended_data;
				uint8_t** out = &sResampleBuffer;
				int outCount = (int64_t)pFrame->nb_samples * sAudioParamTarget.freq / pFrame->sample_rate + 256;
				int outSize = av_samples_get_buffer_size(NULL, sAudioParamTarget.channels, outCount, sAudioParamTarget.fmt, 0);
				if (outSize < 0)
				{
					cout << "av_samples_get_buffer_size() failed" << endl;
					return -1;
				}
				if (sResampleBuffer == NULL)
					av_fast_malloc(&sResampleBuffer, (unsigned int*)&sResampleBufferLen, outSize);
				if (sResampleBuffer == NULL)
					return AVERROR(ENOMEM);
				// 音频重采样：返回值是重采样后得到的音频数据中单个声道的样本数
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
				cpLen = nbSamples * sAudioParamTarget.channels * av_get_bytes_per_sample(sAudioParamTarget.fmt);
			}
			else
			{
				frameSize = av_samples_get_buffer_size(NULL, pCodecContex->channels, pFrame->nb_samples, pCodecContex->sample_fmt, 1);
				cout << "frame size" << frameSize << ", buffer size" << bufferSize << endl;
				pCpBuffer = pFrame->data[0];
				cpLen = frameSize;
			}
			// 将音频数据拷贝到函数输出参数 audioBuffer
			memcpy(audioBuffer, pCpBuffer, cpLen);
			result = cpLen;
			goto EXIT;
		}
		// 2 向解码器喂数据，每次喂一个 packet
		if (needNew)
		{
			ret = avcodec_send_packet(pCodecContex, pPacket);
			if (ret != 0)
			{
				cout << "avcodec_send_packet() failed " << ret << endl;
				av_packet_unref(pPacket);
				result = -1;
				goto EXIT;
			}
		}
	}
EXIT:
	av_frame_unref(pFrame);
	return result;
}

void sdlAudioCallback(void* userdata, uint8_t* stream, int len)
{
	AVCodecContext* pCodecContext = (AVCodecContext*)userdata;
	int copyLen;
	int getSize; // 解码后音频数据大小
	static uint8_t sAudioBuffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	static uint32_t sAudioLen = 0; // 新取得的音频数据大小
	static uint32_t sTxIndex = 0; // 已经发给设备的数据量
	AVPacket* pPacket;
	int frameSize = 0;
	int returnSize = 0;
	int ret;
	while (len > 0) // 确保 stream 缓冲区填满
	{
		if (sDecodeFinished)
			return;
		if (sTxIndex >= sAudioLen)
		{
			pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
			// 1. 从队列中读出一包音频数据
			if (PacketQueuePop(&sAudioPacketQueue, pPacket, 1) <= 0)
			{
				if (sInputFinished)
				{
					pPacket = NULL;
					cout << "Flushing decoder..." << endl;
				}
				else
					return;
			}
			// 2.解码音频包
			getSize = AudioDecodeFrame(pCodecContext, pPacket, sAudioBuffer, sizeof(sAudioBuffer));
			if (getSize < 0)
			{
				// 出错，输出一段静音
				sAudioLen = 1024;
				memset(sAudioBuffer, 0, sAudioLen);
				av_packet_unref(pPacket);
			}
			else if (getSize == 0) // 解码缓冲区被冲洗，整个解码过程完毕
				sDecodeFinished = true;
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
		// 将解码后的音频帧 写入音频设备缓冲区
		memcpy(stream, (uint8_t*)sAudioBuffer + sTxIndex, copyLen);
		len -= copyLen;
		stream += copyLen;
		sTxIndex += copyLen;
	}
}

int main(int argc, char* argv[])
{
	AVFormatContext* pFormatContext = NULL;
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	AVPacket* pPacket = NULL;
	SDL_AudioSpec wantedSpec;
	SDL_AudioSpec actualSpec;

	int i = 0;
	int audioIndex = -1;
	int ret = 0;
	int result = 0;
	if (argc < 2)
	{
		cout << "Please provide a movie file" << endl;
		return -1;
	}

	// A1 构建 AVFormatContext
	// A1.1 打开视频文件：读取文件头，将文件格式信息存储在 fmt context 中
	ret = avformat_open_input(&pFormatContext, argv[1], NULL, NULL);
	if (ret != 0)
	{
		cout << "avoformat_open_input() failed " << ret;
		result = -1;
		goto EXIT0;
	}
	// A1.2 搜索流信息：读取一段文件数据，尝试解码，将取到的流信息填入 pFormat
	ret = avformat_find_stream_info(pFormatContext, NULL);
	if (ret < 0)
	{
		cout << "avformat_find_stream_info() failed " << ret << endl;
		result = -1;
		goto EXIT1;
	}

	// 将文件相关信息打印在标准错误设备上
	av_dump_format(pFormatContext, 0, argv[1], 0);

	// A2 查找第一个音频流
	audioIndex = -1;
	for (i = 0; i < pFormatContext->nb_streams; i++)
	{
		if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioIndex = i;
			cout << "Find a audio stream, index " << audioIndex << endl;
			break;
		}
	}

	if (audioIndex == -1)
	{
		cout << "Can't find audio stream" << endl;
		result = -1;
		goto EXIT1;
	}

	// A3 为音频流构建解码器 AVCodecContext
	// A3.1 获取解码器参数 AVCodecParameters
	pCodecParameters = pFormatContext->streams[audioIndex]->codecpar;

	// A3.2 获取解码器
	pCodec = (AVCodec*)avcodec_find_decoder(pCodecParameters->codec_id);
	if (pCodec == NULL)
	{
		cout << "Can't find codec" << endl;
		result = -1;
		goto EXIT1;
	}

	// A3.3 构建解码器 AVCodecContext
	// A3.3.1 pCodecCtx 初始化：分配结构体，使用pCodec 初始化相应成员为默认值
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		cout << "avcodec_alloc_context3() failed" << endl;
		result = -1;
		goto EXIT1;
	}

	// A3.3.2 pCodecCtx 初始化：pCodecParameteres ==> pCodecContext
	ret = avcodec_parameters_to_context(pCodecContext, pCodecParameters);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context() failed " << ret << endl;
		result = -1;
		goto EXIT2;
	}

	// A3.3.3 pCodecCtx 初始化：使用 pCodec 初始化 pCodecContext，初始化完成
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2() failed " << ret << endl;
		result = -1;
		goto EXIT2;
	}

	pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
	if (pPacket == NULL)
	{
		cout << "av_malloc() failed" << endl;
		result = -1;
		goto EXIT2;
	}

	// B1.初始化SDL 子系统：缺省值(事件处理、文件IO、线程)
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		cout << "SDL_Init() failed " << SDL_GetError() << endl;
		result = -1;
		goto EXIT3;
	}
	PacketQueueInit(&sAudioPacketQueue);
	// B2 打开音频设备并创建音频处理线程
	// B2.1 打开音频设备，获取SDL设备支持的音频参数 actual_spec 
	//  1) SDL提供两种使用音频设备获取音频数据的方式
	//  a.push SDL 以特定频率调用回调函数，在回调函数中获取音频数据
	//  b.pull 用户程序以特定的频率调用 SDL_QueueAudio(), 向音频设备提供数据，此情况 wanted_spec.callback = NULL
	//  2) 音频设备打开后播放静音，不启动回调，调用 SDL_PauseAudio(0)后启动回调，开始正常播放音频
	wantedSpec.freq = pCodecContext->sample_rate; // 采样率
	wantedSpec.format = AUDIO_S16SYS;	// S表带符号，16是采样深度，SYS表采用系统字节序
	wantedSpec.channels = pCodecContext->channels; // 声道数
	wantedSpec.silence = 0; //静音值
	wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;  // SDL 声音缓冲区尺寸，单位是单声道采样点尺寸x通道数
	wantedSpec.callback = sdlAudioCallback; // 回调函数
	wantedSpec.userdata = pCodecContext; // 提供给回调函数的参数
	if (!(audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantedSpec, &actualSpec,
		SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
	{
		cout << "SDL_OpenAudioDevice() failed: " << SDL_GetError() << endl;
		goto EXIT4;
	}

	// B2.2 根据 SDL 音频参数构建音频重采样参数
	if (actualSpec.format != AUDIO_S16SYS)
	{
		cout << "Device isn't support AUDIO_S16SYS" << endl;
		goto EXIT4;
	}
	sAudioParamTarget.fmt = AV_SAMPLE_FMT_S16;
	sAudioParamTarget.freq = actualSpec.freq;
	sAudioParamTarget.channelLayout = av_get_default_channel_layout(actualSpec.channels);
	sAudioParamTarget.channels = actualSpec.channels;
	sAudioParamTarget.frameSize = av_samples_get_buffer_size(NULL, actualSpec.channels, 1, sAudioParamTarget.fmt, 1);
	sAudioParamTarget.bytesPerSec = av_samples_get_buffer_size(NULL, actualSpec.channels, actualSpec.freq, sAudioParamTarget.fmt, 1);
	if (sAudioParamTarget.bytesPerSec <= 0 || sAudioParamTarget.frameSize <= 0)
	{
		cout << "av_samples_get_buffer_size() failed" << endl;
		goto EXIT4;
	}
	sAudioParamSource = sAudioParamTarget;
	// B3 打开音频回调处理
	SDL_PauseAudioDevice(audioDevice, 0);

	// A4.从文件中读取一个 packet
	while (av_read_frame(pFormatContext, pPacket) == 0)
	{
		if (pPacket->stream_index == audioIndex)
			PacketQueuePush(&sAudioPacketQueue, pPacket);
		else
			av_packet_unref(pPacket);
	}
	SDL_Delay(40);
	sInputFinished = true;
	//A5.等待解码结束
	while (!sDecodeFinished)
		SDL_Delay(1000);
	SDL_Delay(1000);
EXIT4:
	SDL_Quit();
EXIT3:
	av_packet_unref(pPacket);
EXIT2:
	avcodec_free_context(&pCodecContext);
EXIT1:
	avformat_close_input(&pFormatContext);
EXIT0:
	if (sResampleBuffer != NULL)
		av_free(sResampleBuffer);
	return result;

}




