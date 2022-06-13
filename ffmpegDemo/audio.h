#pragma once
#include <windows.h>
#include "player.h"

class Audio
{
public:
	Audio();
	BOOL Open();
	void Close();

private:
	BOOL OnDecodeThread();
	int	 AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL OpenAudioStream();
	BOOL AudioResample(INT64 callbackTime);
	BOOL OpenAudioPlaying();
	static BOOL DecodeThread(void* arg);

private:
	FrameQueue		m_frameQueue;					// 音频解码后帧队列
	PacketQueue		m_packetQueue;					// 音频解码前帧队列
	BOOL			m_stop;
	AVStream*		m_pAudioStream;					// 音频流
	SwrContext*		m_swrContext;					// 音频格式转换上下文
	AudioParam		m_audioParamSource;				// 源音频参数(输入数据的参数)
	AudioParam		m_audioParamTarget;				// 目标音频参数
	int				m_audioHardwareBufferSize;		// SDL 音频缓冲区大小（单位字节）
	uint8_t*		m_pAudioFrame;					// 指向待播放的一帧音频数据，指向的数据将被拷入SDL音频缓冲区
	uint8_t*		m_pAudioFrameRwr;				// 音频重采样的输出缓冲区
	unsigned int	m_audioFrameSize;				// 待播放的一帧音频数据大小
	unsigned int	m_audioFrameRwrSize;			// 申请到的音频缓冲区 pAudioFrameRwr 大小
	int				m_audioCopyIndex;				// 当前音频帧中已拷入 SDL 音频缓冲区的位置索引
	int				m_audioWriteBufferSize;			// 当前音频帧中尚未拷入 SDL 音频缓冲区的数据量 audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double			m_audioClock;					// 音频时钟(当前帧播放结束的时间？)
	int				m_audioClockSerial;
	SDL_Thread*		m_decodeThread;					// 音频解码线程
	AVCodecContext* m_pAudioCodecContext;			// 音频编码器上下文
	SDL_AudioDeviceID m_audioDevice;

};

