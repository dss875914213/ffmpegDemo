#include "demux.h"
#include "packet.h"
#include <iostream>
#include <functional>
#include "config.h"
using namespace std;

FFDemux::FFDemux()
	:m_player(NULL),
	m_readThread(NULL),
	m_audioIndex(-1),
	m_videoIndex(-1),
	m_pFormatContext(NULL),
	m_continueReadThread(NULL)
{
	ZeroMemory(&m_audioPacketQueue, sizeof(m_audioPacketQueue));
	ZeroMemory(&m_videoPacketQueue, sizeof(m_videoPacketQueue));
}

FFDemux::~FFDemux()
{

}

BOOL FFDemux::Init(const string& filename, Player* player)
{
	m_player = player;
	// 分配流媒体解析上下文
	AVFormatContext* pFormatContext = avformat_alloc_context();
	INT32 error = 0;
	INT32 ret = 0;
	if (!pFormatContext)
	{
		cout << "Could not allocate context." << endl;
		ret = AVERROR(ENOMEM);
		goto FAIL;
	}

	// 中断回调机制。为底层 I/O 层提供一个处理接口，比如中止 IO 操作
	pFormatContext->interrupt_callback.callback = DecodeInterruptCallback;
	pFormatContext->interrupt_callback.opaque = this;

	// 1.构建 AVFormatContext
	// 1.1 打开视频文件：读取文件头，将文件格式信息存储在 frame context 中
	error = avformat_open_input(&pFormatContext, filename.c_str(), NULL, NULL);
	if (error < 0)
	{
		cout << "avformat_open_input() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	m_pFormatContext = pFormatContext;

	// 1.2 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入 pFormatContext->streams
	// pFormatContext->streams 是一个指针数组，数组大小是 pFormatContext->nb_streams
	error = avformat_find_stream_info(pFormatContext, NULL);
	if (error < 0)
	{
		cout << "avformat_find_stream_info() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	// 2. 查找第一个音频流/ 视频流
	// nb_streams 文件中流媒体个数
	for (INT32 i = 0; i < pFormatContext->nb_streams; i++)
	{
		if ((pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) && (m_audioIndex == -1))
		{
			m_audioIndex = i;
			cout << "Find a audio stream, index " << m_audioIndex << endl;
		}
		if ((pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) && (m_videoIndex == -1))
		{
			m_videoIndex = i;
			cout << "Find a video stream, index " << m_videoIndex << endl;
		}
		if (m_audioIndex != -1 && m_videoIndex != -1)
			break;
	}
	if (m_audioIndex == -1 && m_videoIndex == -1)
	{
		cout << "Can't find any audio/video stream" << endl;
		ret = -1;
	FAIL:
		if (pFormatContext != NULL)
			avformat_close_input(&pFormatContext); // 关闭流媒体上下文
		return ret;
	}

	// 未解压缩数据队列初始化
	if (PacketQueueInit(&m_videoPacketQueue) < 0 || PacketQueueInit(&m_audioPacketQueue) < 0)
		goto FAIL;

	AVPacket flushPacket;
	flushPacket.data = NULL;
	// 将刷新数据放入未解压缩数据队列中
	PacketQueuePut(&m_videoPacketQueue, &flushPacket);
	PacketQueuePut(&m_audioPacketQueue, &flushPacket);

	if (!(m_continueReadThread = SDL_CreateCond()))
		return FALSE;
	return 0;
}


BOOL FFDemux::Open()
{
	// 创建读文件线程 (线程函数, 线程名字, 传给线程的参数)
	m_readThread = SDL_CreateThread(FFDemux::DemuxThread, "demuxThread", this);
	if (m_readThread == NULL)
	{
		cout << "SDL_CreateThread() failed: " << SDL_GetError() << endl;
		return -1;
	}
	return 0;
}

BOOL FFDemux::IsStop()
{
	return m_player->IsStop();
}

BOOL FFDemux::Close()
{
	PacketQueueAbort(&m_videoPacketQueue);
	PacketQueueAbort(&m_audioPacketQueue);
	SDL_WaitThread(m_readThread, NULL);
	avformat_close_input(&m_pFormatContext);
	PacketQueueDestroy(&m_videoPacketQueue);
	PacketQueueDestroy(&m_audioPacketQueue);
	SDL_DestroyCond(m_continueReadThread);
	return TRUE;
}

PacketQueue* FFDemux::GetPacketQueue(BOOL isVideo)
{
	if (isVideo)
		return &m_videoPacketQueue;
	else
		return &m_audioPacketQueue;
}

AVStream* FFDemux::GetStream(BOOL isVideo)
{
	if (isVideo)
		return m_pFormatContext->streams[m_videoIndex];
	else
		return m_pFormatContext->streams[m_audioIndex];
}

BOOL FFDemux::StreamHasEnoughPackets(AVStream* stream, INT32 streamIndex, PacketQueue* queue)
{
	// -DSS TODO 使用 abortRequest 没有加锁
	return streamIndex<0 ||
		queue->abortRequest ||
		(stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		queue->numberPackets>MIN_FRAMES && (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0);
}

BOOL FFDemux::DemuxThread(void* is)
{
	FFDemux* demux = reinterpret_cast<FFDemux*>(is);
	int ret;
	AVPacket pkt1, * pkt = &pkt1;
	// 该互斥量作用不大，没看到用处
	SDL_mutex* waitMutex = SDL_CreateMutex();
	cout << "demux_thread running..." << endl;

	AVStream* videoStream = demux->GetStream(TRUE);
	AVStream* audioStream = demux->GetStream(FALSE);
	// 4. 解复用处理
	while (1)
	{
		// -DSS TODO 解析完，这个线程是不是也要退出
		if (demux->IsStop())
			break;
		// 如果未解码队列中数据足够多，则循环等待

		if (demux->m_audioPacketQueue.size + demux->m_videoPacketQueue.size > MAX_QUEUE_SIZE ||
			(demux->StreamHasEnoughPackets(audioStream, demux->m_audioIndex, &demux->m_audioPacketQueue) &&
				demux->StreamHasEnoughPackets(videoStream, demux->m_videoIndex, &demux->m_videoPacketQueue)))
		{
			SDL_LockMutex(waitMutex);
			// -DSS TODO 读线程时，应该释放该变量；退出时也应该触发这个函数
			SDL_CondWaitTimeout(demux->m_continueReadThread, waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.1 从输入文件中读取一个 packet
		ret = av_read_frame(demux->m_pFormatContext, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				// 输入文件已读完，则往 packet 队列中发送 NULL packet, 以冲洗 flush 解码器,否则解码器中缓存的帧取不出来
				if (demux->m_videoIndex >= 0)
					PacketQueuePutNullPacket(&demux->m_videoPacketQueue, demux->m_videoIndex);
				if (demux->m_audioIndex >= 0)
					PacketQueuePutNullPacket(&demux->m_audioPacketQueue, demux->m_audioIndex);
			}
			SDL_LockMutex(waitMutex);
			SDL_CondWaitTimeout(demux->m_continueReadThread, waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.3 根据当前 packet 类型(音频、视频、字幕),将其存入对应的 packet 队列
		if (pkt->stream_index == demux->m_audioIndex)
			PacketQueuePut(&demux->m_audioPacketQueue, pkt);
		else if (pkt->stream_index == demux->m_videoIndex)
			PacketQueuePut(&demux->m_videoPacketQueue, pkt);
		else
			// 重置 pkt 里面的数据，并将 pkt data 空间释放
			av_packet_unref(pkt);
	}
	// 只是退出该线程，不关闭程序，需要渲染完成才退出
	ret = 0;
	if (ret != 0)
	{
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = demux;
		SDL_PushEvent(&event);
	}
	SDL_DestroyMutex(waitMutex);
	return 0;
}

INT32 FFDemux::DecodeInterruptCallback(void* context)
{
	FFDemux* is = static_cast<FFDemux*>(context);
	return is->IsStop();
}
