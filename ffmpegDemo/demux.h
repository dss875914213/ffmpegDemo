#pragma once
#include <string>
#include <windows.h>
#include "player.h"

using namespace std;

class  FFDemux
{
public:
	FFDemux(string filename);
	~FFDemux();
	BOOL Init();
	BOOL DemuxDeinit();
	BOOL Open();
	BOOL Close();
	void Stop(BOOL flag);

private:
	BOOL StreamHasEnoughPackets(AVStream* stream, int streamIndex, PacketQueue* queue);
	static BOOL DemuxThread(void* mux);
	static int DecodeInterruptCallback(void* context);

private:
	std::string			m_fileName;
	SDL_Thread*			m_readThread;
	BOOL				m_stop;
	PacketQueue			m_audioPacketQueue;		// 音频未解码帧队列
	PacketQueue			m_videoPacketQueue;		// 视频未解码帧队列
	AVFormatContext*	m_pFormatContext;		// 流媒体解析上下文
	AVStream*			m_pAudioStream;			// 音频流
	AVStream*			m_pVideoStream;			// 视频流
	AVCodecContext*		m_pAudioCodecContext;	// 音频编码器上下文
	AVCodecContext*		m_pVideoCodecContext;	// 视频编码器上下文
	int					m_audioIndex;			// 音频流索引
	int					m_videoIndex;			// 视频流索引
	SDL_cond*			m_continueReadThread;	// 读线程信号量
};

