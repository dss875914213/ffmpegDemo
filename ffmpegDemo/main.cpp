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
	AVFrame* pFrmRaw = NULL; // ֡���ɰ�����õ���ԭʼ֡
	AVFrame* pFrmYUV = NULL; // ֡���ɰ�ԭʼ֡ɫ��ת���õ�
	AVPacket* pPacket = NULL; // ���������ж�����һ������
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

	//A1.����Ƶ�ļ�����ȡ�ļ�ͷ�����ļ���ʽ��Ϣ�洢�� fmt context ��
	ret = avformat_open_input(&pFmtCtx, argv[1], NULL, NULL);
	if (ret != 0)
	{
		cout << "avformat_open_input() failed" << endl;
		return -1;
	}

	//A2.��������Ϣ����ȡһ����Ƶ�ļ����ݣ����Խ��룬��ȡ��������Ϣ���� pFormatCtx->streams
	//pFmtCtx->streams ��һ��ָ�����飬�����СΪ pFormatCtx->nb_streams
	ret = avformat_find_stream_info(pFmtCtx, NULL);
	if (ret < 0)
	{
		cout << "avformat_find_stream_info failed" << endl;
		return -1;
	}

	// ���ļ��ȹ���Ϣ��ӡ�ڱ�׼�����豸��
	av_dump_format(pFmtCtx, 0, argv[1], 0);



}





