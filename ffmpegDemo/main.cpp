
#include <iostream>
using namespace std;
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

#include "SDL.h"
#include "SDL_video.h"
#include "SDL_render.h"
#include "SDL_rect.h"

#define SDL_USEREVENT_REFRESH (SDL_USEREVENT+1)
static bool sPlayingExit = false;
static bool sPlayingPause = false;

// �� opaque ����Ĳ���֡�ʲ��������̶����ʱ�䷢��ˢ�� ʱ��
int SDLThreadHandleRefreshing(void* opaque)
{
	SDL_Event sdlEvent;

	int frameRate = *((int*)opaque);
	int interval = (frameRate > 0) ? 1000 / frameRate : 40;
	cout << "Frame rate: " << frameRate << " FPS, refresh interval " << interval << " ms" << endl;
	while (!sPlayingExit)
	{
		if (!sPlayingPause)
		{
			sdlEvent.type = SDL_USEREVENT_REFRESH;
			SDL_PushEvent(&sdlEvent);
		}
		SDL_Delay(interval);
	}
	return 0;
}

int main(int argc, char* argv[])
{
	// Initialize these to NULL prevents segment fault!
	AVFormatContext* pFormatContext = NULL;
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	AVFrame* pFrameRaw = NULL;  // ������ԭʼ֡
	AVFrame* pFrameYUV = NULL;	// ��ʽת�����֡
	AVPacket* pPacket = NULL; // ���ļ��ж�����ѹ������
	struct SwsContext* pSwsContext = NULL;
	int bufSize;
	uint8_t* buffer = NULL;
	int videoIndex = -1;
	int ret;
	int res=0;
	int frameRate;

	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_Thread* sdlThread;
	SDL_Event sdlEvent;

	if (argc < 2)
	{
		cout << "Please provide a movie file" << endl;
		return -1;
	}

	// A1.����Ƶ�ļ�
	ret = avformat_open_input(&pFormatContext, argv[1], NULL, NULL);
	if (ret != 0)
	{
		cout << "avformat_open_input failed " << ret << endl;
		res = -1;
		goto EXIT0;
	}

	// A2.��������Ϣ
	ret = avformat_find_stream_info(pFormatContext, NULL);
	if (ret < 0)
	{
		cout << "avformat_find_stream_info failed " << ret << endl;
		res = -1;
		goto EXIT1;
	}

	// ���ļ������Ϣ��ӡ����׼�����豸��
	av_dump_format(pFormatContext, 0, argv[1], 0);

	// A3.���ҵ�һ����Ƶ��
	for (int i = 0; i < pFormatContext->nb_streams; i++)
	{
		if (pFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoIndex = i;
			cout << "Find a video stream, index " << videoIndex << endl;
			frameRate = pFormatContext->streams[i]->avg_frame_rate.num / pFormatContext->streams[i]->avg_frame_rate.den;
			break;
		}
	}

	if (videoIndex == -1)
	{
		cout << "Can't find a video stream" << endl;
		res = -1;
		goto EXIT1;
	}

	// A5 Ϊ��Ƶ���������� AVCodecContext
	// A5.1 ��ȡ����������
	pCodecParameters = pFormatContext->streams[videoIndex]->codecpar;

	// A5.2 ��ȡ������
	pCodec = avcodec_find_decoder(pFormatContext->streams[videoIndex]->codecpar->codec_id);
	if (pCodec == NULL)
	{
		cout << "Can't find codec" << endl;
		res = -1;
		goto EXIT1;
	}

	// A5.3 ���������� AVCodecContext
	// A5.3.1 pCodecContext ��ʼ��������ṹ�壬��ʼ����Ӧ��ԱΪĬ��ֵ
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		cout << "avcodec_alloc_context3 failed " << endl;
		res = -1;
		goto EXIT1;
	}

	// A5.3.2 pCodecContext ��ʼ����ʹ��pCodecParameters ��ʼ����Ӧ��Ա
	ret = avcodec_parameters_to_context(pCodecContext, pCodecParameters);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context failed " << ret << endl;
		res = -1;
		goto EXIT1;
	}

	// A5.3.3 pCodecContext ��ʼ����ʹ��pCodec ��ʼ����Ӧ��Ա
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2 failed " << ret << endl;
		res = -1;
		goto EXIT2;
	}

	// A6.����Frame
	// A6.1 ����ṹ���������� AVFrame.*data[]
	pFrameRaw = av_frame_alloc();
	if (pFrameRaw == NULL)
	{
		cout << "av_frame_alloc for pFrameRaw failed" << endl;
		res = -1;
		goto EXIT2;
	}
	pFrameYUV = av_frame_alloc();
	if (pFrameYUV == NULL)
	{
		cout << "av_frame_alloc for pFrameYUV failed" << endl;
		res = -1;
		goto EXIT3;
	}

	// A6.2 ΪAVFrame.*data �ֹ����仺����
	bufSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecContext->width, pCodecContext->height, 1);

	buffer = (uint8_t*)av_malloc(bufSize);
	if (buffer == NULL)
	{
		cout << "av_malloc for buffer failed" << endl;
		res = -1;
		goto EXIT4;
	}

	// ʹ�ø��������趨 pFrameYUV->data �� pFrameYUV->linesize
	ret = av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P, pCodecContext->width, pCodecContext->height, 1);
	if (ret < 0)
	{
		cout << "av_image_fill_arrays failed " << ret << endl;
		res = -1;
		goto EXIT5;
	}

	// B1.��ʼ�� SwsContext
	pSwsContext = sws_getContext(pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt,
		pCodecContext->width, pCodecContext->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (pSwsContext == NULL)
	{
		cout << "sws_getContext failed " << endl;
		res = -1;
		goto EXIT6;
	}

	// B2. ���� SDL ����
	screen = SDL_CreateWindow("simple ff player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
		pCodecContext->width, pCodecContext->height, SDL_WINDOW_OPENGL);
	if (screen == NULL)
	{
		cout << "SDL_CreateWindow failed " << endl;
		res = -1;
		goto EXIT7;
	}

	// B3. ���� SDL_Renderer
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	if (sdlRenderer == NULL)
	{
		cout << "SDL_CreateRenderer failed" << endl;
		res = -1;
		goto EXIT7;
	}

	// B4. ���� SDL_Texture
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecContext->width, pCodecContext->height);
	if (sdlTexture == NULL)
	{
		cout << "SDL_CreateTexture failed" << endl;
		res = -1;
		goto EXIT7;
	}

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = pCodecContext->width;
	sdlRect.h = pCodecContext->height;

	pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
	if (pPacket == NULL)
	{
		cout << "av_malloc for pPacket failed" << endl;
		res = -1;
		goto EXIT7;
	}

	// B5.������ʱˢ���¼��̣߳�����Ԥ��Ƶ�ʲ���ˢ���¼�
	sdlThread = SDL_CreateThread(SDLThreadHandleRefreshing, NULL, (void*)&frameRate);
	if (sdlThread == NULL)
	{
		cout << "SDL_CreateThread failed " << SDL_GetError() << endl;
		res = -1;
		goto EXIT8;
	}

	while (1)
	{
		// B6. �ȴ�ˢ���¼�
		SDL_WaitEvent(&sdlEvent);
		if (sdlEvent.type == SDL_USEREVENT_REFRESH)
		{
			// A8.����Ƶ�ļ��ж�ȡһ�� packet
			while (av_read_frame(pFormatContext, pPacket) == 0)
			{
				if (pPacket->stream_index == videoIndex)
					break;
			}

			// A9. ��Ƶ����
			// A9.1 �������ι����
			ret = avcodec_send_packet(pCodecContext, pPacket);
			if (ret != 0)
			{
				cout << "avcodec_send_packet failed " << ret << endl;
				res = -1;
				goto EXIT8;
			}

			// A9.2 ���ս������������
			ret = avcodec_receive_frame(pCodecContext, pFrameRaw);
			if (ret != 0)
			{
				if (ret == AVERROR_EOF)
				{
					cout << "avcodec_receive_frame: the decoder has been fully flushed" << endl;
				}
				else if (ret == AVERROR(EAGAIN))
				{
					cout << "avcodec_receive_frame: output is not available in this state user must try send new input" << endl;
					continue;
				}
				else if (ret == AVERROR(EINVAL))
				{
					cout << "avcodec_receive_frame: codec not opened, or it is an encoder" << endl;
				}
				else
				{
					cout << "avcode_receive_frame: legitimate decoding errors" << endl;
				}
				res = -1;
				goto EXIT8;
			}

			// A10. ͼ��ת��
			sws_scale(pSwsContext, (const uint8_t* const*)pFrameRaw->data, pFrameRaw->linesize, 0, pCodecContext->height, pFrameYUV->data, pFrameYUV->linesize);
			
			// B7. ʹ��YUV ���ݸ��� SDL_rect
			SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
				pFrameYUV->data[0], pFrameYUV->linesize[0],
				pFrameYUV->data[1], pFrameYUV->linesize[1],
				pFrameYUV->data[2], pFrameYUV->linesize[2]);

			// B8. ʹ���ض���ɫ��յ�ǰ��ȾĿ��
			SDL_RenderClear(sdlRenderer);

			// B9. ʹ�� texture ������ȾĿ��
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

			// B10. ִ����Ⱦ
			SDL_RenderPresent(sdlRenderer);

			av_packet_unref(pPacket);
		}
		else if (sdlEvent.type == SDL_QUIT)
		{
			cout << "SDL_event QUIT" << endl;
			sPlayingExit = true;
			break;
		}
	}
EXIT8:
	SDL_Quit();
EXIT7:
	av_packet_unref(pPacket);
EXIT6:
	sws_freeContext(pSwsContext);
EXIT5:
	av_free(buffer);
EXIT4:
	av_frame_free(&pFrameYUV);
EXIT3:
	av_frame_free(&pFrameRaw);
EXIT2:
	avcodec_free_context(&pCodecContext);
EXIT1:
	avformat_close_input(&pFormatContext);
EXIT0:
	return res;

}

















