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
	AVFrame* pFrmRaw = NULL; // ֡���ɰ�����õ���ԭʼ֡
	AVFrame* pFrmYUV = NULL; // ֡���ɰ�ԭʼ֡ɫ��ת���õ�
	AVPacket* pPacket = NULL; // ���������ж�����һ������
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

	//A3.���ҵ�һ����Ƶ��
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

	// A5.Ϊ��Ƶ������������ AVCodecContext
	// A5.1 ��ȡ���������� AVCodecParameters
	pCodecPar = pFmtCtx->streams[videoIndex]->codecpar;
	// A5.2 ��ȡ������
	pCodec = avcodec_find_decoder(pCodecPar->codec_id);
	if (pCodec == NULL)
	{
		cout << "Can't find codec!" << endl;
		return -1;
	}

	// A5.3����������AVCodecContext
	// A5.3.1 pCodecCtx ��ʼ��������ṹ�壬ʹ�� pCodec ��ʼ����Ӧ��ԱΪĬ��ֵ
	pCodecCtx = avcodec_alloc_context3(pCodec);

	// A5.3.2 pCodecCtx ��ʼ���� pCodecPar ==> pCodecCtx, ��ʼ����Ӧ��Ա
	ret = avcodec_parameters_to_context(pCodecCtx, pCodecPar);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context() failed " << ret << endl;
		return -1;
	}
	// A5.3.3 pCodecCtx ��ʼ����ʹ�� pCodec ��ʼ�� pCodecCtx����ʼ�����
	ret = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2 failed " << ret << endl;
		return -1;
	}

	// A6.���� AVFrame
	// A6.1 ���� AVFrame �ṹ��ע�Ⲣ������ data_buffer(��AVFrame.*data[])
	pFrmRaw = av_frame_alloc();
	pFrmYUV = av_frame_alloc();

	// A6.2 ΪAVFrame.*data[] �ֹ����仺���������ڴ洢 sws_scale() ��Ŀ��֡��Ƶ����
	// pFrmRaw �� data_buffer �� av_read_frame() ���䣬��˲����ֹ�����
	bufSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
	// buffer ����Ϊ pFrmYUV ����Ƶ���ݻ�����
	buffer = (uint8_t*)av_malloc(bufSize);
	// ʹ�ø��������趨 pFrmYUV->data �� pFrmYUV->linesize
	av_image_fill_arrays(pFrmYUV->data, pFrmYUV->linesize, buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	// A7.��ʼ�� SWS context, ���ں���ͼ��ת��
	// �˴���6������ʹ�õ��� FFmpeg �е����ظ�ʽ���ԱȲο�ע��B4
	// FFmpeg �е����ظ�ʽ AV_PIX_FMT_YUV420P ��Ӧ SDL �е����ظ�ʽ SDL_PIXELFORMAT_IYUV
	// ��������õ���ͼ�񲻱� SDL ֧�֣�������ͼ��ת����SDL�޷�������ʾͼƬ
	// ��SDL֧�ָø�ʽ������Ҫ����ת��
	// ����Ϊ�˱����㣬ͳһת��ΪSDL֧�ֵĸ�ʽ AV_PIX_FMT_YUV420P ==> SDL_PIXELFORMT_IYUV
	swsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	// B1. ��ʼ��SDL��ϵͳ��ȱʡ(�¼������ļ�IO���߳�)����Ƶ����Ƶ����ʱ��
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		cout << "SDL_Init() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// B2. ����SDL ���ڣ�SDL2.0֧�ֶര��
	// SDL_Window �����г���󵯳�����Ƶ����
	screen = SDL_CreateWindow("Simple FFmpeg player's Windows", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width, pCodecCtx->height, SDL_WINDOW_OPENGL);
	if (screen == NULL)
	{
		cout << "SDL_CreateWindow failed: " << SDL_GetError() << endl;
		return -1;
	}

	// B3. ����SDL_Renderer
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

	// B4. ����SDL_Texture
	// һ��SDL_Texture ��Ӧһ֡YUV���ݣ��˴���2������ʹ�õ���SDL�е����ز�������ӦA7
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = pCodecCtx->width;
	sdlRect.h = pCodecCtx->height;

	pPacket = (AVPacket*)av_malloc(sizeof(AVPacket));

	// A8. ����Ƶ�ļ��ж�ȡһ�� packet
	// packet ��������Ƶ֡����Ƶ֡���������ݣ�������ֻ���������Ƶ֡���������ݻᱻ�ӵ�
	// ������Ƶ��˵��һ�� packet ֻ����һ�� frame
	// ������Ƶ��˵������֡���̶��ĸ�ʽ��һ�� packet �ɰ��������� frame;����֡���ɱ�ĸ�ʽ��һ�� packet ֻ����һ�� frame
	while (av_read_frame(pFmtCtx, pPacket) == 0)
	{
		if (pPacket->stream_index == videoIndex) // ��������Ƶ֡
		{
			// A9. ��Ƶ֡���룺 packet ==> frame
			// A9.1 �������ι���ݣ�һ�� packet ������һ����Ƶ֡������Ƶ֡���˴���Ƶ֡�ѱ�����
			ret = avcodec_send_packet(pCodecCtx, pPacket);
			if (ret != 0)
			{
				cout << "avcodec_send_packet() failed " << ret << endl;
				return -1;
			}

			// A9.2 ���ս�������������ݣ��˴�ֻ������Ƶ֡��ÿ�ν���һ�� packet, ��֮����õ�һ��frame
			ret = avcodec_receive_frame(pCodecCtx, pFrmRaw);
			if (ret != 0)
			{
				cout << "avcodec_receive_frame() " << ret << endl;
				return -1;
			}

			// A10. ͼ��ת���� p_frm_raw->data ==> p_frm_yuv->data
			// ��Դͼ����һƬ���������򾭹��������µ�Ŀ��ͼ���Ӧ���򣬴����ͼ�����������������
			// plane: ��YUV�� Y��U��V ���� plane, RGB�� R��G��B����plane
			// slice: ͼ����һƬ�������У�������������
			// stride/pitch: һ��ͼ����ռ���ֽ����� stride=BytesPerPixel*Width+Padding��ע���ֽڶ���
			// AVFrame.*data[]: ÿ������Ԫ��ָ���Ӧ plane
			// AVFrame.linesize[]: ÿ������Ԫ�ر�ʾ��Ӧ plane ��һ��ͼ����ռ���ֽ���
			sws_scale(swsCtx, (const uint8_t* const*)pFrmRaw->data, pFrmRaw->linesize, 0, pCodecCtx->height, pFrmYUV->data, pFrmYUV->linesize);

			// B5. ʹ���µ�YUV�������ݸ���SDL_Rect
			SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
				pFrmYUV->data[0], pFrmYUV->linesize[0],
				pFrmYUV->data[1], pFrmYUV->linesize[1],
				pFrmYUV->data[2], pFrmYUV->linesize[2]);

			// B6.ʹ���ض���ɫ��յ�ǰ��ȾĿ��
			SDL_RenderClear(sdlRenderer);
			// B7.ʹ�ò���ͼ������(texture)���µ�ǰ��ȾĿ��
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);

			// B8.ִ����Ⱦ��������Ļ��ʾ
			SDL_RenderPresent(sdlRenderer);

			// B9.����֡��Ϊ25FPS���˴�����׼ȷ��δ���ǽ�������ʱ��
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





