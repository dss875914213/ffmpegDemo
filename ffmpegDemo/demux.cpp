#include "demux.h"
#include "packet.h"
#include <iostream>
using namespace std;

static int DecodeInterruptCallback(void* context)
{
	PlayerStation* is = static_cast<PlayerStation*>(context);
	return is->abortRequest;
}

static int DemuexInit(PlayerStation* is)
{
	AVFormatContext* pFormatContext = NULL;
	int error;
	int ret;
	int audioIndex;
	int videoIndex;
	pFormatContext = avformat_alloc_context();
	if (!pFormatContext)
	{
		cout << "Could not allocate context." << endl;
		ret = AVERROR(ENOMEM);
		goto FAIL;
	}

	// 中断回调机制。为底层 I/O 层提供一个处理接口，比如中止 IO 操作
	pFormatContext->interrupt_callback.callback = DecodeInterruptCallback;
	pFormatContext->interrupt_callback.opaque = is;

	// 1.构建 AVFormatContext
	// 1.1 打开视频文件：读取文件头，将文件格式信息存储在 frame context 中
	error = avformat_open_input(&pFormatContext, is->filename, NULL, NULL);
	if (error < 0)
	{
		cout << "avformat_open_input() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	is->pFormatContext = pFormatContext;

	// 1.2 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入 pFormatContext->streams
	// ic->streams 是一个指针数组，数组大小是 pFormatContext->nb_streams
	error = avformat_find_stream_info(pFormatContext, NULL);
	if (error < 0)
	{
		cout << "avformat_find_stream_info() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}

	// 2. 查找第一个音频流/ 视频流
	audioIndex = -1;
	videoIndex = -1;
	for (int i = 0; i < pFormatContext->nb_streams; i++)
	{
		if ((pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) && (audioIndex == -1))
		{
			audioIndex = i;
			cout << "Find a audio stream, index " << audioIndex << endl;
		}
		if ((pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) && (videoIndex == -1))
		{
			videoIndex = i;
			cout << "Find a video stream, index " << videoIndex << endl;
		}
		if (audioIndex != -1 && videoIndex != -1)
			break;
	}
	if (audioIndex == -1 && videoIndex == -1)
	{
		cout << "Can't find any audio/video stream" << endl;
		ret = -1;
	FAIL:
		if (pFormatContext != NULL)
			avformat_close_input(&pFormatContext);
		return ret;
	}
	is->audioIndex = audioIndex;
	is->videoIndex = videoIndex;
	is->pAudioStream = pFormatContext->streams[audioIndex];
	is->pVideoStream = pFormatContext->streams[videoIndex];
	return 0;
}

int DemuxDeinit()
{
	return 0;
}

static int StreamHasEnoughPackets(AVStream* stream, int streamIndex, PacketQueue* queue)
{
	return streamIndex<0 ||
		queue->abortRequest ||
		(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		queue->numberPackets>MIN_FRAMES && (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0);
}

static int DemuxThread(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	AVFormatContext* pFormatContext = is->pFormatContext;
	int ret;
	AVPacket pkt1, * pkt = &pkt1;
	SDL_mutex* waitMutex = SDL_CreateMutex();
	cout << "demux_thread running..." << endl;

	// 4. 解复用处理
	while (1)
	{
		if (is->abortRequest)
			break;
		if (is->audioPacketQueue.size + is->videoPacketQueue.size > MAX_QUEUE_SIZE ||
			(StreamHasEnoughPackets(is->pAudioStream, is->audioIndex, &is->audioPacketQueue) &&
				StreamHasEnoughPackets(is->pVideoStream, is->videoIndex, &is->videoPacketQueue)))
		{
			SDL_LockMutex(waitMutex);
			SDL_CondWaitTimeout(is->continueReadThread, waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.1 从输入文件中读取一个 packet
		ret = av_read_frame(is->pFormatContext, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				// 输入文件已读完，则往 packet 队列中发送 NULL packet, 以冲洗 flush 解码器,否则解码器中缓存的帧取不出来
				if (is->videoIndex >= 0)
					PacketQueuePutNullPacket(&is->videoPacketQueue, is->videoIndex);
				if (is->audioIndex >= 0)
					PacketQueuePutNullPacket(&is->audioPacketQueue, is->audioIndex);
			}
			SDL_LockMutex(waitMutex);
			SDL_CondWaitTimeout(is->continueReadThread, waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.3 根据当前 packet 类型(音频、视频、字幕),将其存入对应的 packet 队列
		if (pkt->stream_index == is->audioIndex)
			PacketQueuePut(&is->audioPacketQueue, pkt);
		else if (pkt->stream_index == is->videoIndex)
			PacketQueuePut(&is->videoPacketQueue, pkt);
		else
			av_packet_unref(pkt);
	}
	ret = 0;
	if (ret != 0)
	{
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	SDL_DestroyMutex(waitMutex);
	return 0;
}

int OpenDemux(PlayerStation* is)
{
	if (DemuexInit(is) != 0)
	{
		cout << "demux_init() failed" << endl;
		return -1;
	}

	is->readThreadID = SDL_CreateThread(DemuxThread, "demuxThread", is);
	if (is->readThreadID == NULL)
	{
		cout << "SDL_CreateThread() failed: " << SDL_GetError() << endl;
		return -1;
	}
	return 0;
}

