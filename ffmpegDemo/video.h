#pragma once
#include "player.h"
#include <windows.h>

static int QueuePicture(PlayerStation* is, AVFrame* sourceFrame, double pts, double duration, int64_t pos);
static int VideoDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
static int VideoDecodeThread(void* arg);
static double ComputeTargetDelay(double delay, PlayerStation* is);
static double VpDuration(PlayerStation* is, Frame* vp, Frame* nextvp);
static void UpdateVideoPts(PlayerStation* is, double pts, int serial);
static void VideoDisplay(PlayerStation* is);
static void VideoRefresh(void* opaque, double* remainingTime);
static int VideoPlayingThread(void* arg);
static int OpenVideoPlaying(void* arg);
static int OpenVideoStream(PlayerStation* is);


int OpenVideo(PlayerStation* is);

class Video
{
public:
	Video();
	~Video();
	BOOL Open();
	void Close();
private:
	BOOL QueuePicture(AVFrame* sourceFrame, double pts, double duration, int64_t pos);
	int  DecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL OnDecodeThread();
	BOOL OnPlayingThread();
	double ComputeTargetDelay(double delay);
	double VpDuration(Frame* vp, Frame* nextvp);
	void UpdatePts(double pts, int serial);
	void Display();
	void Refresh(double* remainingTime);
	int OpenPlaying();
	int OpenStream();
	static BOOL DecodeThread(void* arg);
	static BOOL PlayingThread(void* arg);

private:
	BOOL				m_stop;
	BOOL				m_paused;							// 暂停
	AVStream*			m_pStream;				// 视频流
	AVCodecContext*		m_pCodecContext;		// 视频编码器上下文	
	PlayClock			m_videoPlayClock;		// 视频播放时钟
	double				m_frameTimer;			// 当前帧开始播放的时间
	PacketQueue			m_packetQueue;			// 视频未解码帧队列
	FrameQueue			m_frameQueue;			// 视频解码帧队列
	SwsContext*			m_swsContext;			// 视频转换上下文
	AVFrame*			m_pFrameYUV;			// 转换格式后的视频数据
	SDL_Thread*			m_playThread;			// 视频渲染线程
	SDL_Thread*			m_decodeThread;			// 视频解码线程
	SDLVideo			m_sdlVideo;
	AVFormatContext*	m_pFormatContext;		// 流媒体解析上下文
	PlayClock			m_audioPlayClock;		// 音频播放时钟 // -DSS TODO 从外界传过来
};
