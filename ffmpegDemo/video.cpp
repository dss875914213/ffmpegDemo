#include <iostream>
#include "video.h"
#include "packet.h"
#include "frame.h"
#include "player.h"
using namespace std;

Video::Video()
{
	// -DSS TODO ������ʼ��
}

Video::~Video()
{

}

BOOL Video::Open()
{
	// ������Ƶ�����������Ĳ�������Ƶ�������߳�
	OpenStream();
	// ��ʼ����ʽת��������, ��ʼ��SDL ͼ����Ⱦ����, ������Ƶ��Ⱦ�߳�
	OpenPlaying();
	return 0;
}

void Video::Close()
{

}

BOOL Video::QueuePicture(AVFrame* sourceFrame, double pts, double duration, int64_t pos)
{
	// �����β������һ����д��֡�ռ䣬���޿ռ���ȴ�
	Frame* frame = FrameQueuePeekWritable(&m_frameQueue);

	if (frame == NULL)
		return FALSE;

	frame->sar = sourceFrame->sample_aspect_ratio; // ��Ƶ�ݺ��
	frame->width = sourceFrame->width; // ʹ�ý���֡�Ŀ��
	frame->height = sourceFrame->height; // ʹ�ý���֡�ĸ߶�
	frame->format = sourceFrame->format; // ʹ�ý���֡�ĸ�ʽ

	frame->pts = pts; // ����ʱ���ת�������Ⱦʱ���
	frame->duration = duration; // ��Ⱦ����ʱ��   ֡�� ��ĸ/ ����
	frame->pos = pos; // frame ��Ӧ packet �������ļ��еĵ�ַƫ��

	// �� AVFrame ���������Ӧλ��
	// �ı� data ָ���ָ��
	// �� dst ָ�� src ,���� src ��ΪĬ��ֵ
	av_frame_move_ref(frame->frame, sourceFrame);
	// ���¶��м�����д����
	FrameQueuePush(&m_frameQueue);
	return TRUE;
}

int Video::DecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	int ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			// ֹͣ����
			if (pPacketQueue->abortRequest)
				return -1;

			// 3. �ӽ��������� frame
			// 3.1 һ����Ƶ packet ����һ�� frame
			// ����������һ�������� packet �󣬲��н����� frame ���
			// frame ���˳���ǰ� pts ��˳���� IBBPBBP
			// frame->pkt_pos �����Ǵ� frame��Ӧ��packet ����Ƶ�ļ��е�ƫ�Ƶ�ַ��ֵͬ pkt.pos
			ret = avcodec_receive_frame(pCodecContext, frame);
			if (ret < 0)
			{
				// decode �Ѿ�����ˢ����
				if (ret == AVERROR_EOF)
				{
					av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): the decoder has been fully flushed\n");
					avcodec_flush_buffers(pCodecContext);
					return 0;
				}
				// �������ݲ���������Ҫ�������ݲ����������֡
				else if (ret == AVERROR(EAGAIN))
				{
					av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): output is not available in this state - "
						"user must try to send new input\n");
					break;
				}
				else
				{
					av_log(NULL, AV_LOG_ERROR, "video avcodec_receive_frame(): other errors\n");
					continue;
				}
			}
			else
			{
				// ʹ�ø�������ʽ�������Ƶ�֡ʱ���������ʱ����
				frame->pts = frame->best_effort_timestamp;
				return 1; // �ɹ�����õ�һ����Ƶ֡
			}
		}

		// 1.ȡ��һ�� packet.ʹ�� pkt ��Ӧ�� serial ��ֵ�� d->pkt_serial
		if (PacketQueueGet(pPacketQueue, &pkt, true) < 0)
			return -1;
		// packet queue �е�һ������ flush_pkt. ÿ�� seek ��������� flush_pkt,���� serial,�����µĲ�������
		// û�������ˣ����ˢ �����������ģ����ڲ�����������³���
		if (pkt.data == NULL)
			avcodec_flush_buffers(pCodecContext);
		else
		{
			// 2.��packet���͸�������
			// ���� packet ��˳���ǰ� dts ������˳��,��IPBBPBB
			// pkt.pos �������Ա�ʶ��ǰ packet ����Ƶ�ļ��еĵ�ַƫ��
			if (avcodec_send_packet(pCodecContext, &pkt) == AVERROR(EAGAIN))
				av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
			// ���� pkt ��������ݣ����� pkt data �ռ��ͷ�
			av_packet_unref(&pkt);
		}
	}
}

BOOL Video::OnDecodeThread()
{
	// ���� frame �ռ�, buffer �ռ���Ҫ���·���
	AVFrame* pFrame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	int gotPicture;
	// ʱ���
	AVRational timebase = m_pStream->time_base;
	// �²�֡��
	AVRational frameRate = av_guess_frame_rate(m_pFormatContext, m_pStream, NULL);
	if (pFrame == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc() for pFrame failed\n");
		return AVERROR(ENOMEM);
	}

	while (1)
	{
		// ֹͣ����
		if (m_stop)
			break;
		// �� packet_queue ��ȡһ�� packet, �������� frame
		gotPicture = VideoDecodeFrame(m_pCodecContext, &m_packetQueue, pFrame);
		if (gotPicture < 0)
			goto EXIT;
		duration = (frameRate.num && frameRate.den ? av_q2d(AVRational{ frameRate.den, frameRate.num }) : 0);// ��ǰ֡����ʱ�� ֡�� ��ĸ/����
		pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase); // ��ǰ��ʾʱ���
		// ����������ݣ������������
		ret = QueuePicture(pFrame, pts, duration, pFrame->pkt_pos); // ����ǰ֡ѹ�� frameQueue
		// av_frame_unref�������������ͷ�����Ƶ������Դ������ frame ���ó�ֵ
		av_frame_unref(pFrame);
		if (ret < 0)
			goto EXIT;
	}
EXIT:
	// av_frame_free���ͷ�������Դ����������Ƶ������Դ�ͽṹ�屾����ڴ�
	av_frame_free(&pFrame);
	return 0;
}

BOOL Video::OnPlayingThread()
{
	double remainingTime = 0.0; // ��ǰ֡����չʾ��ʱ�� (��λ s)
	while (1)
	{
		// ֹͣ����
		if (m_stop)
			break;
		if (remainingTime > 0.0)
			av_usleep((unsigned)(remainingTime * 1000000.0));
		remainingTime = REFRESH_RATE;
		// ������ʾ��ǰ֡,����ʱ remaining_time ������ʾ
		Refresh(&remainingTime);
	}
	return 0;
}

double Video::ComputeTargetDelay(double delay)
{
	double syncThreshold, diff = 0;
	// ��Ƶʱ����ͬ��ʱ�ӵĲ��죬ʱ��ֵ����һ֡ pts ֵ��ʵΪ����һ֡pts+��һ֡�������ŵ�ʱ��
	// ��Ƶpts + ��Ƶ��һ֡֡��Ⱦʱ�� - ��Ƶ pts - ��Ƶ��һ֡֡��Ⱦʱ��
	diff = GetClock(&m_videoPlayClock) - GetClock(&m_audioPlayClock);
	// delay ����һ֡����ʱ������ǰ֡�������ŵ�֡������ʱ������һ֡ʱ�������ֵ
	// diff ����Ƶʱ����ͬ��ʱ�ӵĲ�ֵ
	// �� delay<AV_SYNC_THRESHOLD_MIN,��ͬ����ֵΪ AV_SYNC_THRESHOLD_MIN
	// �� delay>AV_SYNC_THRESHOLD_MAX,��ͬ����ֵΪ AV_SYNC_THRESHOLD_MAX
	// ��AV_SYNC_THRESHOLD_MIN<delay<AV_SYNC_THRESHOLD_MAX, ��ͬ����ֵΪ delay
	syncThreshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
	if (!isnan(diff))
	{
		// ����Ƶʱ�������ͬ��ʱ�ӣ��ҳ���ͬ����ֵ����ǰ��Ƶ������Ⱦʱ��
		if (diff <= -syncThreshold)
			delay = FFMAX(0, delay + diff); // ��ǰ֡����ʱ�������ͬ��ʱ�� (delay+diff<0)��delay = 0(��Ƶ׷�ϣ���������),����delay=deley+diff
		// ��Ƶʱ�ӳ�ǰ��ͬ��ʱ�ӣ��ҳ���ͬ����ֵ������һ֡����ʱ������
		else if (diff >= syncThreshold && delay > AV_SYNC_FRAMEUP_THRESHOLD)
			delay += diff; // ����У��Ϊ delay += diff, ��Ҫ�� AV_SYNC_FRAMEDUP_THRESHOLD ����������
		// ��Ƶʱ�ӳ�ǰ��ͬ��ʱ�ӣ��ҳ���ͬ����ֵ
		else if (diff >= syncThreshold)
			delay = 2 * delay; // ��Ƶ�����ٶȷ����Ų�, delay������2��
	}
	av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);
	return delay;
}

double Video::VpDuration(Frame* vp, Frame* nextvp)
{
	if (vp->serial == nextvp->serial)
	{
		double duration = nextvp->pts - vp->pts;
		if (isnan(duration) || duration <= 0)
			return vp->duration;
		else
			return duration;
	}
	else
		return 0.0;
}

void Video::UpdatePts(double pts, int serial)
{
	SetClock(&m_videoPlayClock, pts, serial); // ���� videoClock
}

void Video::Display()
{
	Frame* videoPicture;
	// ȡ����֡���в��ţ�ֻ��ȡ��ɾ��
	videoPicture = FrameQueuePeekLast(&m_frameQueue);
	// ͼƬת���� pFrameRaw->data ==> pFrameYUV->data
	// ��Դͼ����һƬ���������򾭹��������µ�Ŀ��ͼ���Ӧ���򣬴����ͼ�����������������
	// plane:��YUV�� Y��U��V����plane��RGB�� R��G��B ���� plane
	// slice: ͼ����һƬ��������, ������������, ˳���ɶ������ײ����ɵײ�������
	// stride/pitch: һ��ͼ����ռ���ֽ���, stride = BytesPerPixel*width+Padding,ע�����
	// AVFrame.*data[]:ÿ������Ԫ��ָ���Ӧ plane
	// AVFrame.linesize[]:ÿ������Ԫ�ر�ʾ��Ӧ plane ��һ��ͼ����ռ���ֽ���
	sws_scale(m_swsContext, static_cast<const uint8_t* const*>(videoPicture->frame->data),
		videoPicture->frame->linesize, 0, m_pCodecContext->height, m_pFrameYUV->data,
		m_pFrameYUV->linesize);

	// ʹ���µ� YUV �������ݸ��� SDL_Rect
	SDL_UpdateYUVTexture(m_sdlVideo.texture, &m_sdlVideo.rect,
		m_pFrameYUV->data[0], m_pFrameYUV->linesize[0],
		m_pFrameYUV->data[1], m_pFrameYUV->linesize[1],
		m_pFrameYUV->data[2], m_pFrameYUV->linesize[2]);

	// ʹ���ض���ɫ��յ�ǰ��ȾĿ��
	SDL_RenderClear(m_sdlVideo.renderer);
	// ʹ�ò���ͼ������(texture)���µ�ǰ��ȾĿ��
	SDL_RenderCopy(m_sdlVideo.renderer, m_sdlVideo.texture, NULL, &m_sdlVideo.rect);
	// ִ����Ⱦ��������Ļ��ʾ
	SDL_RenderPresent(m_sdlVideo.renderer);
}

void Video::Refresh(double* remainingTime)
{
	double time;
	// �Ƿ�����֡
	static bool firstFrame = true;

RETRY:
	// ��ȡ frameQueue ��δ��ʾ��֡��
	if (FrameQueueNumberRemaining(&m_frameQueue) == 0) // ����֡����ʾ
		return;
	double lastDuration, duration, delay;
	Frame* vp, * lastvp;
	lastvp = FrameQueuePeekLast(&m_frameQueue);  // ��һ֡���ϴ�����ʾ��֡
	vp = FrameQueuePeek(&m_frameQueue); // ��ǰ֡����ǰ֡��ʾ��֡
	// lastvp �� vp ����ͬһ���������� (һ�� seek �Ὺʼһ���µĲ�������),��frame timer ����Ϊ��ǰʱ��
	if (firstFrame)
	{
		m_frameTimer = av_gettime_relative() / 1000000.0;
		firstFrame = false;
	}

	// ��ͣ������ͣ������һ֡ͼ��
	if (m_paused)
		goto DISPLAY;
	lastDuration = VpDuration(lastvp, vp); // ��һ֡���۲���ʱ����vp->pts - lastvp->pts
	delay = ComputeTargetDelay(lastDuration); // ������Ƶʱ�Ӻ�ͬ��ʱ�ӵĲ�ֵ������ delay ֵ
	time = av_gettime_relative() / 1000000.0;
	// ��ǰ֡����ʱ��(is->frame_timer + delay) ���ڵ�ǰʱ�� (time), ��ʾ����ʱ��δ��
	if (time < m_frameTimer + delay)
	{
		// ����ʱ��δ���������ˢ��ʱ�� remaining_time Ϊ��ǰʱ�̵���һ����ʱ�̵�ʱ���
		*remainingTime = FFMIN(m_frameTimer + delay - time, *remainingTime);
		// ����ʱ��δ��, �򲻲���, ֱ�ӷ���
		return;
	}
	// ����frame_timer ֵ
	m_frameTimer += delay;
	// У�� frame_timer ֵ���� frame_timer ����ڵ�ǰϵͳʱ��̫��(�������ͬ����ֵ),����µ�ǰϵͳʱ��
	if (delay > 0 && time - m_frameTimer > AV_SYNC_THRESHOLD_MAX)
		m_frameTimer = time;
	SDL_LockMutex(m_frameQueue.mutex);
	if (!isnan(vp->pts))
		UpdatePts(vp->pts, vp->serial); // ������Ƶʱ�ӣ�ʱ�����ʱ��ʱ��
	SDL_UnlockMutex(m_frameQueue.mutex);

	// �Ƿ�Ҫ����δ�ܼ�ʱ���ŵ���Ƶ֡
	if (FrameQueueNumberRemaining(&m_frameQueue) > 1) // ������δ��ʾ֡��>1(ֻ��һ֡�򲻿��Ƕ�֡)
	{
		Frame* nextvp = FrameQueuePeekNext(&m_frameQueue); // ��һ֡:��һ֡����ʾ��֡
		duration = VpDuration(vp, nextvp); // ��һ֡���۲���ʱ�� = nextvp->pts - vp->pts
		// ��ǰ֡ vp δ�ܼ�ʱ����, ����һ֡�������ʱ��(is->frameTimer+duration)С�ڵ�ǰϵͳʱ��(time)
		if (time > m_frameTimer + duration)
		{
			FrameQueueNext(&m_frameQueue); // ɾ����һ֡����ʾ֡, ��ɾ�� lastvp, ��ָ���1(��lastvp���µ�vp)
			goto RETRY;
		}
	}

	// ɾ����ǰ��ָ��Ԫ��, ��ָ��+1. ��δ��֡,��ָ���lastvp���µ�vp;���ж�֡,��ָ���vp���µ�nextvp
	FrameQueueNext(&m_frameQueue);
DISPLAY:
	Display(); // ȡ����ǰ֡ vp(���ж�֡�� nextvp)���в���
}

int Video::OpenPlaying()
{
	int ret;
	// ��Ƶ��������С
	int bufferSize;
	// ��Ƶ��������ַ
	uint8_t* buffer = NULL;
	// �����ʽת����� frame
	m_pFrameYUV = av_frame_alloc();
	if (m_pFrameYUV == NULL)
	{
		cout << "av_frame_alloc() for p_frm_raw failed" << endl;
		return -1;
	}
	// ΪAVFrame.*data[] �ֹ����仺���������ڴ洢 sws_scale ��Ŀ��֡��Ƶ����
	// ��Ƶ��ʽ, ��Ƶ���, ��Ƶ�߶�, ��Ƶ�����ֽ���
	// �˴� bufferSize = width*height*1.5
	bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, m_pCodecContext->width, m_pCodecContext->height, 1);
	// ���û������ռ�
	buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
	if (buffer == NULL)
	{
		cout << "av_malloc() for buffer failed" << endl;
		return -1;
	}

	// ʹ�ø��������趨 pFrameYUV->data �� pFrameYUV->linesize
	ret = av_image_fill_arrays(m_pFrameYUV->data, m_pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
		m_pCodecContext->width, m_pCodecContext->height, 1);
	if (ret < 0)
	{
		cout << "av_image_fill_arrays() failed " << ret << endl;
		return -1;
	}

	// A2. ��ʼ�� SWS context, ���ں���ͼ��ת��
	m_swsContext = sws_getContext(m_pCodecContext->width, m_pCodecContext->height,
		m_pCodecContext->pix_fmt, m_pCodecContext->width, m_pCodecContext->height,
		AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (m_swsContext == NULL)
	{
		cout << "sws_getContext() failed" << endl;
		return -1;
	}

	// SDL_Rect ��ֵ
	m_sdlVideo.rect.x = 0;
	m_sdlVideo.rect.y = 0;
	m_sdlVideo.rect.w = m_pCodecContext->width;
	m_sdlVideo.rect.h = m_pCodecContext->height;

	// 1.����SDL���� (��������, ������ʼ��x, ������ʼ��y, ���ڿ��w, ���ڸ߶�h, 
	m_sdlVideo.window = SDL_CreateWindow("simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		m_sdlVideo.rect.w, m_sdlVideo.rect.h, SDL_WINDOW_OPENGL);
	if (m_sdlVideo.window == NULL)
	{
		cout << "SDL_CreateWindow() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 2.����SDL_Renderer (��ȾͼƬ�Ĵ���, ѡ���Կ�, ���)
	m_sdlVideo.renderer = SDL_CreateRenderer(m_sdlVideo.window, -1, 0);
	if (m_sdlVideo.renderer == NULL)
	{
		cout << "SDL_CreateRenderer() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 3.���� SDL_Texture (��Ⱦ��������, ���ظ�ʽ, ����Ȩ��(�Ƿ񾭳��޸�), �����, �����)
	m_sdlVideo.texture = SDL_CreateTexture(m_sdlVideo.renderer,
		SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, m_sdlVideo.rect.w, m_sdlVideo.rect.h);
	if (m_sdlVideo.texture == NULL)
	{
		cout << "SDL_CreateTexture() failed " << SDL_GetError() << endl;
		return -1;
	}
	// ������Ƶ��Ⱦ�߳�
	m_playThread = SDL_CreateThread(PlayingThread, "video playing thread", this);
	return 0;
}

int Video::OpenStream()
{
	// ��������
	AVCodecParameters* pCodecPar = NULL;
	// �������
	AVCodec* pCodec = NULL;
	// �������������
	AVCodecContext* pCodecContext = NULL;
	// ��Ƶ��
	AVStream* pStream = m_pStream;
	int ret;
	// 1.Ϊ��Ƶ������������ AVCodecContext
	// 1.1 ��ȡ���������� AVCodecParameters
	pCodecPar = pStream->codecpar;

	// 1.2 ��ȡ������
	pCodec = const_cast<AVCodec*>(avcodec_find_decoder(pCodecPar->codec_id));
	if (pCodecPar == NULL)
	{
		cout << "Can't find codec" << endl;
		return -1;
	}

	// 1.3 ���������� AVCodecContext
	// 1.3.1 pCodecContext ��ʼ��: ����ṹ�壬ʹ�� pCodec ��ʼ����Ӧ��ԱΪĬ��ֵ
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		cout << "avcodec_alloc_context3() failed" << endl;
		return -1;
	}

	// 1.3.2 pCodecContext ��ʼ��: pCodecPar ==> pCodecContext ��ʼ����Ӧ��Ա
	ret = avcodec_parameters_to_context(pCodecContext, pCodecPar);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context() failed" << endl;
		return -1;
	}

	// 1.3.3 pCodecContext ��ʼ��: ʹ�� pCodec ��ʼ�� pCodecContext ��ʼ�����
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2() failed " << ret << endl;
		return -1;
	}
	// ������Ƶ������������
	m_pCodecContext = pCodecContext;

	// 2.������Ƶ�����߳�
	m_decodeThread = SDL_CreateThread(DecodeThread, "video decode thread", this);
	return 0;
}

BOOL Video::DecodeThread(void* arg)
{
	Video* is = static_cast<Video*>(arg);
	is->OnDecodeThread();
}

BOOL Video::PlayingThread(void* arg)
{
	Video* is = static_cast<Video*>(arg);
	is->OnPlayingThread();
}
