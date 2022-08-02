#pragma once
#include <string>
#include "player.h"
#include <windows.h>

using namespace std;

class Player;
class  FFDemux
{
public:
	FFDemux();
	~FFDemux();
	BOOL					Init(const string& filename, Player* player); // 初始化
	BOOL					Open();	// 创建读文件线程 (线程函数, 线程名字, 传给线程的参数)
	BOOL					Close();	// 关闭
	PacketQueue*			GetPacketQueue(BOOL isVideo);  // 得到对应队列
	AVStream*				GetStream(BOOL isVideo);	// 得到对应流

private:
	BOOL					IsStop();	// 是否停止
	BOOL					StreamHasEnoughPackets(AVStream* stream, INT32 streamIndex, PacketQueue* queue);
	static BOOL				DemuxThread(void* mux);
	static INT32			DecodeInterruptCallback(void* context);

private:
	AVFormatContext*	m_pFormatContext;		// 流媒体解析上下文
	INT32				m_audioIndex;			// 音频流索引
	INT32				m_videoIndex;			// 视频流索引

	Player*				m_player;				// 播放器
	PacketQueue			m_audioPacketQueue;		// 音频未解码帧队列
	PacketQueue			m_videoPacketQueue;		// 视频未解码帧队列

	SDL_cond*			m_continueReadThread;	// 读线程信号量
	SDL_Thread*			m_readThread;			// 读线程

};

