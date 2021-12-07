#include <iostream>
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include <libavutil/imgutils.h>
}
#include "SDL.h"
#include "SDL_video.h"
#include "SDL_render.h"
#include "SDL_rect.h"

using namespace std;


int main(int argc, char* argv[])
{
	// Initialize these to NULL prevents seg fault!
	AVFormatContext* pFmtCtx = NULL;
	AVCodecContext* pCodecCtx = NULL;
	AVCodecParameters* pCodecPar = NULL;
	AVCodec* pCodec = NULL;
	AVFrame* pFrmRaw = NULL; // 帧，由包解码得到的原始帧
	AVFrame* pFrmYUV = NULL; // 帧，由包原始帧色彩转换得到
	AVPacket* pPacket = NULL; // 包，从流中读出的一段数据
	struct SwsContext* swsCtx = NULL;
	int bufSize = 0;
	uint8_t* buffer = NULL;
	int i;
	int videoIndex;
	int ret;

	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;

	if (argc < 2)
	{
		cout << "Please provide a movie file" << endl;
		return -1;
	}

	//A1.打开视频文件：读取文件头，将文件格式信息存储在 fmt context 中
	ret = avformat_open_input(&pFmtCtx, argv[1], NULL, NULL);
	if (ret != 0)
	{
		cout << "avformat_open_input() failed" << endl;
		return -1;
	}

	//A2.搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入 pFormatCtx->streams
	//pFmtCtx->streams 是一个指针数组，数组大小为 pFormatCtx->nb_streams
	ret = avformat_find_stream_info(pFmtCtx, NULL);
	if (ret < 0)
	{
		cout << "avformat_find_stream_info failed" << endl;
		return -1;
	}

	// 将文件先关信息打印在标准错误设备上
	av_dump_format(pFmtCtx, 0, argv[1], 0);

	//A3.查找第一个视频流
	videoIndex = -1;
	for (i = 0; i < pFmtCtx->nb_streams; i++)
	{
		if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoIndex = i;
			cout << "Find a video stream, index " << videoIndex << endl;
			break;
		}
	}
	if (videoIndex == -1)
	{
		cout << "Can't find a video stream!" << endl;
		return -1;
	}

	// A5.为视频流构建解码器 AVCodecContext
	// A5.1 获取解码器参数 AVCodecParameters
	pCodecPar = pFmtCtx->streams[videoIndex]->codecpar;
	// A5.2 获取解码器
	pCodec = avcodec_find_decoder(pCodecPar->codec_id);
	if (pCodec == NULL)
	{
		cout << "Can't find codec!" << endl;
		return -1;
	}

	// A5.3构建解码器AVCodecContext
	// A5.3.1 pCodecCtx 初始化：分配结构体，使用 pCodec 初始化相应成员为默认值
	pCodecCtx = avcodec_alloc_context3(pCodec);

	// A5.3.2 pCodecCtx 初始化： pCodecPar ==> pCodecCtx, 初始化相应成员
	ret = avcodec_parameters_to_context(pCodecCtx, pCodecPar);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context() failed " << ret << endl;
		return -1;
	}
	// A5.3.3 pCodecCtx 初始化：使用 pCodec 初始化 pCodecCtx，初始化完成
	ret = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2 failed " << ret << endl;
		return -1;
	}

	// A6.分配 AVFrame
	// A6.1 分配 AVFrame 结构，注意并不分配 data_buffer(即AVFrame.*data[])
	pFrmRaw = av_frame_alloc();
	pFrmYUV = av_frame_alloc();

	// A6.2 为AVFrame.*data[] 手工分配缓冲区，用于存储 sws_scale() 中目的帧视频数据
	// pFrmRaw 的 data_buffer 由 av_read_frame() 分配，因此不需手工分配
	bufSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
	// buffer 将作为 pFrmYUV 的视频数据缓冲区
	buffer = (uint8_t*)av_malloc(bufSize);
	// 使用给定参数设定 pFrmYUV->data 和 pFrmYUV->linesize
	av_image_fill_arrays(pFrmYUV->data, pFrmYUV->linesize, buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	// A7.初始化 SWS context, 用于后续图像转换
	// 此处第6个参数使用的是 FFmpeg 中的像素格式，对比参考注释B4
	// FFmpeg 中的像素格式 AV_PIX_FMT_YUV420P 对应 SDL 中的像素格式 SDL_PIXELFORMAT_IYUV
	// 如果解码后得到的图像不被 SDL 支持，不进行图像转换，SDL无法正常显示图片
	// 如SDL支持该格式，则不需要进行转换
	// 这里为了编码简便，统一转换为SDL支持的格式 AV_PIX_FMT_YUV420P ==> SDL_PIXELFORMT_IYUV
	swsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	// B1. 初始化SDL子系统：缺省(事件处理、文件IO、线程)、视频、音频、定时器
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		cout << "SDL_Init() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// B2. 创建SDL 窗口，SDL2.0支持多窗口
	// SDL_Window 即运行程序后弹出的视频窗口
	screen = SDL_CreateWindow("Simple FFmpeg player's Windows", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL);
	if (screen == NULL)
	{
		cout << "SDL_CreateWindow failed: " << SDL_GetError() << endl;
		return -1;
	}

	// B3. 创建SDL_Renderer
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

	// B4. 创建SDL_Texture
	// 一个SDL_Texture 对应一帧YUV数据，此处第2个参数使用的是SDL中的像素参数，对应A7
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = pCodecCtx->width;
	sdlRect.h = pCodecCtx->height;

	pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));

	// A8. 从视频文件中读取一个 packet
	// packet 可能是视频帧、音频帧或其他数据，解码器只会解码音视频帧，其他数据会被扔掉
	// 对于视频来说，一个 packet 只包含一个 frame
	// 对于音频来说，若是帧长固定的格式则一个 packet 可包括整数个 frame;若是帧长可变的格式则一个 packet 只包含一个 frame
	while (av_read_frame(pFmtCtx, pPacket) == 0)
	{
		if (pPacket->stream_index == videoIndex) // 仅处理视频帧
		{
			// A9. 视频帧解码： packet ==> frame
			// A9.1 向解码器喂数据，一个 packet 可能是一个视频帧或多个音频帧，此处音频帧已被过滤
			ret = avcodec_send_packet(pCodecCtx, pPacket);
			if (ret != 0)
			{
				cout << "avcodec_send_packet() failed " << ret << endl;
				return -1;
			}

			// A9.2 接收解码器输出的数据，此处只处理视频帧，每次接收一个 packet, 将之解码得到一个frame
			ret = avcodec_receive_frame(pCodecCtx, pFrmRaw);
			if (ret != 0)
			{
				cout << "avcodec_receive_frame() " << ret << endl;
				return -1;
			}

			// A10. 图像转换： p_frm_raw->data ==> p_frm_yuv->data
			// 将源图像中一片连续的区域经过处理后更新到目标图像对应区域，处理的图像区域必须逐行连续
			// plane: 如YUV有 Y、U、V 三个 plane, RGB有 R、G、B三个plane
			// slice: 图像中一片连续的行，必须是连续的
			// stride/pitch: 一行图像所占得字节数， stride=BytesPerPixel*Width+Padding，注意字节对齐
			// AVFrame.*data[]: 每个数组元素指向对应 plane
			// AVFrame.linesize[]: 每个数组元素表示对应 plane 中一行图像所占得字节数
			sws_scale(swsCtx, (const uint8_t* const*)pFrmRaw->data, pFrmRaw->linesize, 0, pCodecCtx->height, pFrmYUV->data, pFrmYUV->linesize);

			// B5. 使用新的YUV像素数据更新SDL_Rect
			SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
				pFrmYUV->data[0], pFrmYUV->linesize[0],
				pFrmYUV->data[1], pFrmYUV->linesize[1],
				pFrmYUV->data[2], pFrmYUV->linesize[2]);

			// B6.使用特定颜色清空当前渲染目标
			SDL_RenderClear(sdlRenderer);
			// B7.使用部分图像数据(texture)更新当前渲染目标
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

			// B8.执行渲染，更新屏幕显示
			SDL_RenderPresent(sdlRenderer);

			// B9.控制帧率为25FPS，此处不够准确，未考虑解码消耗时间
			SDL_Delay(40);
		}
		av_packet_unref(pPacket);
	}

	SDL_Quit();
	sws_freeContext(swsCtx);
	av_free(buffer);
	av_frame_free(&pFrmYUV);
	av_frame_free(&pFrmRaw);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFmtCtx);
	return 0;
}





