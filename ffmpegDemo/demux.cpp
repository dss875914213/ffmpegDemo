#include "demux.h"
#include "packet.h"
#include <iostream>
#include <functional>
using namespace std;

FFDemux::FFDemux(string filename)
	:m_fileName(filename),
	m_stop(TRUE),
	m_audioIndex(-1),
	m_videoIndex(-1),
	m_pFormatContext(NULL),
	m_pAudioStream(NULL),
	m_pVideoStream(NULL),
	m_pAudioCodecContext(NULL),
	m_pVideoCodecContext(NULL),
	m_continueReadThread(NULL)
{

}

FFDemux::~FFDemux()
{

}

BOOL FFDemux::Init()
{
	// ������ý�����������
	AVFormatContext* pFormatContext = avformat_alloc_context();
	int error;
	int ret;
	// ����Ƶ�ֱ��Ӧ��������
	if (!pFormatContext)
	{
		cout << "Could not allocate context." << endl;
		ret = AVERROR(ENOMEM);
		goto FAIL;
	}

	// �жϻص����ơ�Ϊ�ײ� I/O ���ṩһ������ӿڣ�������ֹ IO ����
	pFormatContext->interrupt_callback.callback = DecodeInterruptCallback;
	pFormatContext->interrupt_callback.opaque = this;

	// 1.���� AVFormatContext
	// 1.1 ����Ƶ�ļ�����ȡ�ļ�ͷ�����ļ���ʽ��Ϣ�洢�� frame context ��
	error = avformat_open_input(&pFormatContext, m_fileName.c_str(), NULL, NULL);
	if (error < 0)
	{
		cout << "avformat_open_input() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}
	m_pFormatContext = pFormatContext;

	// 1.2 ��������Ϣ����ȡһ����Ƶ�ļ����ݣ����Խ��룬��ȡ��������Ϣ���� pFormatContext->streams
	// pFormatContext->streams ��һ��ָ�����飬�����С�� pFormatContext->nb_streams
	error = avformat_find_stream_info(pFormatContext, NULL);
	if (error < 0)
	{
		cout << "avformat_find_stream_info() failed " << error << endl;
		ret = -1;
		goto FAIL;
	}

	// 2. ���ҵ�һ����Ƶ��/ ��Ƶ��
	// nb_streams �ļ�����ý�����
	for (int i = 0; i < pFormatContext->nb_streams; i++)
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
			// �ر���ý��������
			avformat_close_input(&pFormatContext);
		return ret;
	}
	// ������Ƶ��
	m_pAudioStream = pFormatContext->streams[m_audioIndex];
	// ������Ƶ��
	m_pVideoStream = pFormatContext->streams[m_videoIndex];
	return 0;
}

BOOL FFDemux::DemuxDeinit()
{
	return 0;
}

BOOL FFDemux::Open()
{
	// ��·��������ʼ��
	if (!Init())
	{
		cout << "demux_init() failed" << endl;
		return -1;
	}

	// �������ļ��߳� (�̺߳���, �߳�����, �����̵߳Ĳ���)
	m_readThread = SDL_CreateThread(FFDemux::DemuxThread, "demuxThread", this);
	if (m_readThread == NULL)
	{
		cout << "SDL_CreateThread() failed: " << SDL_GetError() << endl;
		return -1;
	}
	return 0;
}

BOOL FFDemux::Close()
{

}

void FFDemux::Stop(BOOL flag)
{
	m_stop = flag;
}

BOOL FFDemux::StreamHasEnoughPackets(AVStream* stream, int streamIndex, PacketQueue* queue)
{
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

	// 4. �⸴�ô���
	while (1)
	{
		if (demux->m_stop)
			break;
		// ���δ��������������㹻�࣬��ѭ���ȴ�
		if (demux->m_audioPacketQueue.size + demux->m_videoPacketQueue.size > MAX_QUEUE_SIZE ||
			(demux->StreamHasEnoughPackets(demux->m_pAudioStream, demux->m_audioIndex, &demux->m_audioPacketQueue) &&
				demux->StreamHasEnoughPackets(demux->m_pVideoStream, demux->m_videoIndex, &demux->m_videoPacketQueue)))
		{
			SDL_LockMutex(waitMutex);
			// TODO ���߳�ʱ��Ӧ���ͷŸñ���
			SDL_CondWaitTimeout(demux->m_continueReadThread, waitMutex, 10);
			SDL_UnlockMutex(waitMutex);
			continue;
		}

		// 4.1 �������ļ��ж�ȡһ�� packet
		ret = av_read_frame(demux->m_pFormatContext, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				// TODO ��һ�ο�֡�ͺ��ˣ��������Ƶ��������ϣ����˳����߳�

				// �����ļ��Ѷ��꣬���� packet �����з��� NULL packet, �Գ�ϴ flush ������,����������л����֡ȡ������
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

int FFDemux::DecodeInterruptCallback(void* context)
{
	PlayerStation* is = static_cast<PlayerStation*>(context);
	return is->abortRequest;
}
