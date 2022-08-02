#pragma once

// 导入 ffmpeg 头文件
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/frame.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
}

// 导入 SDL 头文件
#include "SDL/SDL.h"
#include "SDL/SDL_video.h"
#include "SDL/SDL_render.h"
#include "SDL/SDL_rect.h"
#include "SDL/SDL_mutex.h"

// 参数配置
#define AV_SYNC_THRESHOLD_MIN 0.04
#define AV_SYNC_THRESHOLD_MAX 0.1
#define AV_SYNC_FRAMEUP_THRESHOLD 0.1
#define AV_NOSYNC_THRESHOLD 10.0
#define REFRESH_RATE 0.01
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000
#define MAX_QUEUE_SIZE (15*1024*1024)
#define MIN_FRAMES 25
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))
#define FF_QUIT_EVENT (SDL_USEREVENT+2)

typedef struct
{
	double pts;				// 当前帧渲染时间戳
	double ptsDrift;		// 当前帧显示时间戳与当前系统时钟时间的差值
	double lastUpdated;		// 当前时钟，最后一次更新时间
	double speed;			// 时钟速度控制，用于控制播放速度
	int serial;				// 播放序列，一个 seek 操作会启动一段新的播放序列
	int paused;				// 暂停标志
	int* queueSerial;		// 指向 packet_serial
}PlayClock;

typedef struct
{
	int freq;					// 采样频率
	int channels;				// 通道数
	int64_t channelLayout;		// 通道布局
	enum AVSampleFormat fmt;	// 音频格式
	int frameSize;				// 音频帧的长度 = 通道数 * 每个样本所占字节数
	int bytesPerSec;			// 每秒的字节数 = 通道数 * 每个样本所占字节数 * 每秒采样数
}AudioParam;

typedef struct
{
	SDL_Window* window;			// 窗口
	SDL_Renderer* renderer;		// 渲染
	SDL_Texture* texture;		// 纹理
	SDL_Rect rect;				// 窗口矩形区域
}SDLVideo;

typedef struct
{
	AVPacketList* firstPacket;	// 第一帧未解码数据，从此处取数据解码
	AVPacketList* lastPacket;	// 最后一帧未解码数据，将新获取的数据放入此帧后面
	int numberPackets;			// 未解码数据帧数
	int size;					// 
	int64_t duration;			// 持续播放时间
	int abortRequest;			// 停止请求
	int serial;					// 播放序列
	SDL_mutex* mutex;			// 互斥锁
	SDL_cond* cond;				// 条件变量
}PacketQueue;

typedef struct
{
	AVFrame* frame;		// 解码后数据
	int serial;
	double pts;			// 渲染时间
	double duration;	// 渲染持续时间
	int64_t pos;		// frame 对应 packet 在输入文件中的地址偏移
	int width;			// 宽度
	int height;			// 高度
	int format;			// 格式
	AVRational sar;		// 样本横纵比 Sample aspect ratio 
	int flipV;			// 垂直镜像
}Frame;

typedef struct
{
	Frame queue[FRAME_QUEUE_SIZE];	// 解码数据队列
	int rindex;						// 读索引
	int windex;						// 写索引
	int maxSize;					// 队列中可存储解码数据最大帧数
	int keepLast;					// 是否保存上一帧数据
	int rindexShown;				// 当前是否有帧在显示
	int size;						// 总帧数
	SDL_mutex* mutex;				// 互斥锁
	SDL_cond* cond;					// 条件变量
	PacketQueue* packetQueue;		// 对应的 packetQueue 未解码数据队列
}FrameQueue;
