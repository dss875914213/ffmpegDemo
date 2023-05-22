#include "demux.h"
#include "packet.h"
#include <iostream>
#include <functional>
#include "config.h"
using namespace std;

FFDemux::FFDemux(Player& player)
	:m_player(player),
	m_readThread(NULL),
	m_audioIndex(-1),
	m_videoIndex(-1)
{
	ZeroMemory(&m_audioPacketQueue, sizeof(m_audioPacketQueue));
	ZeroMemory(&m_videoPacketQueue, sizeof(m_videoPacketQueue));
}

FFDemux::~FFDemux()
{

}

BOOL FFDemux::Init(const string& filename)
{
	// ������ý�����������
	AVFormatContext* pFormatContext = avformat_alloc_context();
	m_pFormatContext.reset(pFormatContext, [](AVFormatContext* context) {avformat_close_input(&context); });
	pFormatContext = NULL;
	INT32 error = 0;
	INT32 ret = 0;
	AVFormatContext* context = NULL;
	if (!m_pFormatContext)
	{
		cout << "Could not allocate context." << endl;
		ret = AVERROR(ENOMEM);
		goto FAIL;
	}

	// �жϻص����ơ�Ϊ�ײ� I/O ���ṩһ������ӿڣ�������ֹ IO ����
	m_pFormatContext->interrupt_callback.callback = DecodeInterruptCallback;
	m_pFormatContext->interrupt_callback.opaque = this;

	// 1.���� AVFormatContext
	// 1.1 ����Ƶ�ļ�����ȡ�ļ�ͷ�����ļ���ʽ��Ϣ�洢�� frame context ��
	context = m_pFormatContext.get();
	error = avformat_open_input(&(context), filename.c_str(), NULL, NULL);
	if (error < 0)
	{
		cout << "avformat_open_input() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	context = NULL;

	// 1.2 ��������Ϣ����ȡһ����Ƶ�ļ����ݣ����Խ��룬��ȡ��������Ϣ���� pFormatContext->streams
	// pFormatContext->streams ��һ��ָ�����飬�����С�� pFormatContext->nb_streams
	error = avformat_find_stream_info(m_pFormatContext.get(), NULL);
	if (error < 0)
	{
		cout << "avformat_find_stream_info() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	// 2. ���ҵ�һ����Ƶ��/ ��Ƶ��
	// nb_streams �ļ�����ý�����
	for (INT32 i = 0; i < m_pFormatContext->nb_streams; i++)
	{
		if ((m_pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) && (m_audioIndex == -1))
		{
			m_audioIndex = i;
			cout << "Find a audio stream, index " << m_audioIndex << endl;
		}
		if ((m_pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) && (m_videoIndex == -1))
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
		goto FAIL;
	}

	// δ��ѹ�����ݶ��г�ʼ��
	if (PacketQueueInit(&m_videoPacketQueue) < 0 || PacketQueueInit(&m_audioPacketQueue) < 0)
		goto FAIL;

	AVPacket flushPacket;
	flushPacket.data = NULL;
	// ��ˢ�����ݷ���δ��ѹ�����ݶ�����
	PacketQueuePut(&m_videoPacketQueue, &flushPacket);
	PacketQueuePut(&m_audioPacketQueue, &flushPacket);

	m_continueReadThread.reset(SDL_CreateCond(), [](SDL_cond* cond) {SDL_DestroyCond(cond); });

	if (!m_continueReadThread)
		return FALSE;
	return TRUE;
FAIL:
	m_pFormatContext.reset();// �ر���ý��������
	return ret;
}


BOOL FFDemux::Open()
{
	// �������ļ��߳� (�̺߳���, �߳�����, �����̵߳Ĳ���)
	m_readThread.reset(SDL_CreateThread(FFDemux::DemuxThread, "demuxThread", this), [](SDL_Thread* thread) {SDL_WaitThread(thread, NULL); });
	if (!m_readThread)
	{
		cout << "SDL_CreateThread() failed: " << SDL_GetError() << endl;
		return FALSE;
	}
	return TRUE;
}

BOOL FFDemux::IsStop()
{
	return m_player.IsStop();
}

BOOL FFDemux::Close()
{
	PacketQueueAbort(&m_videoPacketQueue);
	PacketQueueAbort(&m_audioPacketQueue);
	m_readThread.reset();
	m_pFormatContext.reset();
	PacketQueueDestroy(&m_videoPacketQueue);
	PacketQueueDestroy(&m_audioPacketQueue);
	m_continueReadThread.reset();
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
	// -DSS TODO ʹ�� abortRequest û�м���
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
	// �û��������ò���û�����ô�
	SDL_mutex* waitMutex = SDL_CreateMutex();
	cout << "demux_thread running..." << endl;

	AVStream* videoStream = demux->GetStream(TRUE);
	AVStream* audioStream = demux->GetStream(FALSE);
	// 4. �⸴�ô���
	while (1)
	{
		// -DSS TODO �����꣬����߳��ǲ���ҲҪ�˳�
		if (demux->IsStop())
			break;
		// ���δ��������������㹻�࣬��ѭ���ȴ�

		if (demux->m_audioPacketQueue.size + demux->m_videoPacketQueue.size > MAX_QUEUE_SIZE ||
			(demux->StreamHasEnoughPackets(audioStream, demux->m_audioIndex, &demux->m_audioPacketQueue) &&
				demux->StreamHasEnoughPackets(videoStream, demux->m_videoIndex, &demux->m_videoPacketQueue)))
		{
			SDL_LockMutex(waitMutex);
			// -DSS TODO ���߳�ʱ��Ӧ���ͷŸñ������˳�ʱҲӦ�ô����������
			SDL_CondWaitTimeout(demux->m_continueReadThread.get(), waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.1 �������ļ��ж�ȡһ�� packet
		ret = av_read_frame(demux->m_pFormatContext.get(), pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				// �����ļ��Ѷ��꣬���� packet �����з��� NULL packet, �Գ�ϴ flush ������,����������л����֡ȡ������
				if (demux->m_videoIndex >= 0)
					PacketQueuePutNullPacket(&demux->m_videoPacketQueue, demux->m_videoIndex);
				if (demux->m_audioIndex >= 0)
					PacketQueuePutNullPacket(&demux->m_audioPacketQueue, demux->m_audioIndex);
			}
			SDL_LockMutex(waitMutex);
			SDL_CondWaitTimeout(demux->m_continueReadThread.get(), waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.3 ���ݵ�ǰ packet ����(��Ƶ����Ƶ����Ļ),��������Ӧ�� packet ����
		if (pkt->stream_index == demux->m_audioIndex)
			PacketQueuePut(&demux->m_audioPacketQueue, pkt);
		else if (pkt->stream_index == demux->m_videoIndex)
			PacketQueuePut(&demux->m_videoPacketQueue, pkt);
		else
			// ���� pkt ��������ݣ����� pkt data �ռ��ͷ�
			av_packet_unref(pkt);
	}
	// ֻ���˳����̣߳����رճ�����Ҫ��Ⱦ��ɲ��˳�
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
