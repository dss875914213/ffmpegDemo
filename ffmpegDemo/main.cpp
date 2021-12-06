#include <iostream>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL.h"
#include "SDL_video.h"
#include "SDL_render.h"
#include "SDL_rect.h"

using namespace std;


int main(int argc, char* argv[])
{
	// Initialize these to NULL prevents segfault!
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
	int v_idx;
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



}





