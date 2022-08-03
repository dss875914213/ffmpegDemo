#include "audio.h"
#include "packet.h"
#include "frame.h"

Audio::Audio()
	:m_swrContext(NULL),
	m_pAudioFrame(NULL),
	m_pAudioFrameRwrBuffer(NULL),
	m_pAudioCodecContext(NULL),
	m_player(NULL),
	m_packetQueue(NULL),
	m_pStream(NULL),
	m_decodeThread(NULL),
	m_audioFrameRwrSize(0),
	m_audioHardwareBufferSize(0),
	m_audioFrameSize(0),
	m_audioCopyIndex(0),
	m_audioClock(0)
{
	ZeroMemory(&m_frameQueue, sizeof(m_frameQueue));
	ZeroMemory(&m_audioParamSource, sizeof(m_audioParamSource));
	ZeroMemory(&m_audioPlayClock, sizeof(m_audioPlayClock));
}

BOOL Audio::Init(PacketQueue* pPacketQueue, Player* player)
{
	m_packetQueue = pPacketQueue;
	if (FrameQueueInit(&m_frameQueue, pPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0)
		return FALSE;
	m_player = player;
	InitClock(&m_audioPlayClock);
	m_pStream = player->GetDemux()->GetStream(FALSE);
	return TRUE;
}

BOOL Audio::Open()
{
	OpenAudioStream(); // ������Ƶ����������(����ʱ���), ������Ƶ�����߳�
	OpenAudioPlaying(); // ����Ƶ�����豸�������ûص�����
	return TRUE;
}

void Audio::Close()
{
	SDL_PauseAudioDevice(m_audioDevice, 1);
	FrameQueueSignal(&m_frameQueue);
	PacketQueueAbort(m_packetQueue);
	SDL_WaitThread(m_decodeThread, NULL);
	FrameQueueDestroy(&m_frameQueue);
}

PlayClock* Audio::GetClock()
{
	return &m_audioPlayClock;
}

BOOL Audio::OnDecodeThread()
{
	AVFrame* pFrame = av_frame_alloc();
	Frame* audioFrame;

	BOOL receive = FALSE;
	AVRational timebase;
	int ret = 0;

	if (pFrame == NULL)
		return AVERROR(ENOMEM);

	while (1)
	{
		if (m_player->IsStop())
			break;
		// ����
		receive = AudioDecodeFrame(m_pAudioCodecContext, m_packetQueue, pFrame);
		if (!receive)
			goto END;

		timebase = AVRational{ 1, pFrame->sample_rate };
		if (!(audioFrame = FrameQueuePeekWritable(&m_frameQueue)))
			goto END;
		// ����ֻ��ʱ����Ļ��㡣�����ǻ��㵽��ʵʱ��
		audioFrame->pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase);
		// ��ǰ֡�����ģ�����������������/�����ʾ��ǵ�ǰ֡�Ĳ���ʱ��
		audioFrame->duration = av_q2d(AVRational{ pFrame->nb_samples, pFrame->sample_rate });
		// �� frame ���ݿ��� audioFrame->frame��audioFrame->frameָ����Ƶ frame ����β��
		av_frame_move_ref(audioFrame->frame, pFrame);
		// ������Ƶ frame ���д�С��дָ��
		FrameQueuePush(&m_frameQueue);
	}
END:
	av_frame_free(&pFrame);
	return ret;
}

void Audio::SetClockAt(PlayClock* clock, DOUBLE pts, DOUBLE time)
{
	clock->pts = pts;	// ������Ⱦʱ��
	clock->lastUpdated = time;	// �����ϴθ���ʱ��
	clock->ptsDrift = clock->pts - time;	// ��ǰ֡��ʾʱ����뵱ǰϵͳʱ��ʱ��Ĳ�ֵ
}

void Audio::SetClock(PlayClock* clock, DOUBLE pts)
{
	// time ��λ(ns)
	double time = av_gettime_relative() / 1000000.0; // av_gettime_relative ��ȡ��ĳ��δָ����������ĵ�ǰʱ�䣨��΢��Ϊ��λ��
	SetClockAt(clock, pts, time);
}

void Audio::InitClock(PlayClock* clock)
{
	SetClock(clock, NAN);
}

void Audio::OnSDLAudioCallback(UINT8* stream, INT32 needSize)
{
	// �ص���ʼ��ʱ��
	INT64 audioCallbackTime = av_gettime_relative();

	if (m_player->IsPause())
	{
		memset(stream, 0, needSize);
		return;
	}
	while (needSize > 0)
	{
		// ����������Χ����ȥ��ȡ������
		if (m_audioCopyIndex >= static_cast<INT32>(m_audioFrameSize))
		{
			// 1.����Ƶ frame ������ȡ��һ�� frame��ת��Ϊ��Ƶ�豸֧�ֵĸ�ʽ������ֵ���ز�����Ƶ֡�Ĵ�С
			INT32 audioSize = AudioResample(audioCallbackTime);
			if (audioSize < 0)
			{
				m_pAudioFrame = NULL;
				m_audioFrameSize = SDL_AUDIO_MIN_BUFFER_SIZE / m_audioParamTarget.frameSize * m_audioParamTarget.frameSize;
			}
			else
				m_audioFrameSize = audioSize;
			m_audioCopyIndex = 0;
		}
		// ���� is->audioCopyIndex �����ã���ֹһ֡��Ƶ���ݴ�С���� SDL ��Ƶ��������С������һ֡������Ҫ������ο���
		// �� is->audioCopyIndex ��ʶ�ز���֡���ѿ��� SDL ��Ƶ������������λ�������� len1 ��ʶ���ο�����������
		INT32 copySize = m_audioFrameSize - m_audioCopyIndex;
		if (copySize > needSize)
			copySize = needSize;
		// 2.��ת�������Ƶ���ݿ�������Ƶ������ stream �У�֮��Ĳ��ž�����Ƶ�豸����������
		if (m_pAudioFrame != NULL)
			memcpy(stream, static_cast<UINT8*>(m_pAudioFrame) + m_audioCopyIndex, copySize);
		else
			memset(stream, 0, copySize);
		needSize -= copySize;
		stream += copySize;
		m_audioCopyIndex += copySize;
	}

	// 3.����ʱ��
	if (!isnan(m_audioClock))
	{
		// -DSS TODO ��� clock �ǲ���Ҫ����
		// ������Ƶʱ�ӣ�����ʱ�̣�ÿ���������������������ݺ�
		// ǰ�� audioDecodeFrame �и��µ� is->audioClock ������Ƶ֡Ϊ��λ�����Դ˴��ڶ�������Ҫ��ȥδ����������ռ�õ�ʱ��
		// 2 * m_audioHardwareBufferSize ����ȥһ����ʾ����ص�֡����Ⱦʱ�䣬�ڼ�ȥһ����ʾ��ǰ��������֡����Ⱦʱ��

		INT32 remainSize = m_audioFrameSize - m_audioCopyIndex;
		SetClockAt(&m_audioPlayClock, m_audioClock - static_cast<DOUBLE>(2 * m_audioHardwareBufferSize + remainSize) /
			m_audioParamTarget.bytesPerSec, audioCallbackTime / 1000000.0);
	}
}

BOOL Audio::AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	INT32 ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			if (m_player->IsStop())
				return TRUE;
			// 3.2 һ����Ƶ packet ��һ����� frame��ÿ�� avcodec_receive_frame() ����һ�� frame
			ret = avcodec_receive_frame(pCodecContext, frame);
			// -DSS TODO �ܻ�ȡ���ݣ��ǲ���ҲҪ�����ݷŵ�����������棬�о�������һ��
			if (ret >= 0)
			{
				// ʱ���任���� d->avcontex->packetTimebase ʱ��ת���� 1/frame->sampleRate ʱ��
				AVRational timebase = AVRational{ 1, frame->sample_rate };
				if (frame->pts != AV_NOPTS_VALUE)
					// pts ����ת��
					frame->pts = av_rescale_q(frame->pts, pCodecContext->pkt_timebase, timebase);
				else
					av_log(NULL, AV_LOG_WARNING, "frame->pts no\n");
				return TRUE;
			}
			else if (ret == AVERROR_EOF)
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): the decoder has been flushed\n");
				avcodec_flush_buffers(pCodecContext);
				return 0;
			}
			else if (ret == AVERROR(EAGAIN))
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): input is not accepted in the current state\n");
				break;
			}
			else
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): other errors\n");
				continue;
			}
		}
		// 1.ȡ��һ�� packet, ʹ�� packet ��Ӧ�� serial ��ֵ�� d->packetSerial
		if (PacketQueueGet(pPacketQueue, &pkt, TRUE) < 0)
			return FALSE;
		// packetQueue �е�һ������ flushPacket��ÿ�� seek ��������� flushPacket������ serial, �����µĲ�������
		if (pkt.data == NULL)
			avcodec_flush_buffers(pCodecContext);  // ��λ�������ڲ�״̬/ˢ���ڲ����������� seek �������л���ʱӦ���ô˺���
		else
		{
			// 2.�� packet ���͸�������
			// ���͸� packet ��˳�� dts ����˳��
			// packet.pos �������Ա�ʶ��ǰ packet ����Ƶ�ļ��еĵ�ַƫ��
			if (avcodec_send_packet(pCodecContext, &pkt) == AVERROR(EAGAIN))
				av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
			av_packet_unref(&pkt);
		}
	}
	return TRUE;
}

BOOL Audio::OpenAudioStream()
{
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	INT32 ret;

	// 1.Ϊ��Ƶ������������ AVCodecContext
	// 1.1 ��ȡ���������� AVCodecParameters
	pCodecParameters = m_pStream->codecpar;
	// 1.2 ��ȡ������
	pCodec = const_cast<AVCodec*>(avcodec_find_decoder(pCodecParameters->codec_id));
	if (pCodec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't find codec!\n");
		return -1;
	}

	// 1.3 ���������� AVCodecContext
	// 1.3.1 pCodecContext ��ʼ��������ṹ�壬ʹ�� pCodec ��ʼ����Ӧ�ĳ�Ա����ΪĬ��ֵ
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3() failed\n");
		return -1;
	}

	// 1.3.2 pCodecContext ��ʼ����pCodecParameters ==>pCodecContext, ��ʼ����Ӧ��Ա
	ret = avcodec_parameters_to_context(pCodecContext, pCodecParameters);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context() failed %d\n", ret);
		return -1;
	}

	// 1.3.3 pCodecContext ��ʼ����ʹ�� pCodec ��ʼ�� pCodecContext����ʼ�����
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodecc_open2() failed %d\n", ret);
		return -1;
	}
	// ʱ���׼
	pCodecContext->pkt_timebase = m_pStream->time_base;
	// ������Ƶ����������
	m_pAudioCodecContext = pCodecContext;

	// 2.������Ƶ�����߳�
	m_decodeThread = SDL_CreateThread(DecodeThread, "audio decode thread", this);
	return 0;
}

INT32 Audio::AudioResample(INT64 callbackTime)
{
	INT32 resampledDataSize;
	INT64 decodeChannelLayout;
	INT32 wantedNumberSamples;
	Frame* audioFrame;

	// ������û��֡��ȴ�
	while (FrameQueueNumberRemaining(&m_frameQueue) == 0)
	{
		// �ȴ�ʱ��̫�������˳�
		if ((av_gettime_relative() - callbackTime) > 1000000LL * m_audioHardwareBufferSize / m_audioParamTarget.bytesPerSec / 2)
			return -1;
		av_usleep(1000);
	}

	// ������ͷ���ɶ������� audioFrame ָ��ɶ�֡
	if (!(audioFrame = FrameQueuePeekReadable(&m_frameQueue)))
		return -1;
	// ɾ����һ֡
	FrameQueueNext(&m_frameQueue);

	// ��ȡ��������
	// �ж� frame ����� channel_layout �� channels �Ƿ��Ӧ
	decodeChannelLayout = (audioFrame->frame->channel_layout &&
		audioFrame->frame->channels == av_get_channel_layout_nb_channels(audioFrame->frame->channel_layout)) ?
		audioFrame->frame->channel_layout : av_get_default_channel_layout(audioFrame->frame->channels);
	wantedNumberSamples = audioFrame->frame->nb_samples;

	// is->audioParamTarget �� SDL �ɽ��ܵ���Ƶ֡��
	if (audioFrame->frame->format != m_audioParamSource.fmt ||
		decodeChannelLayout != m_audioParamSource.channelLayout ||
		audioFrame->frame->sample_rate != m_audioParamSource.freq)
	{
		swr_free(&m_swrContext);
		// �����ز�������
		m_swrContext = swr_alloc_set_opts(NULL, m_audioParamTarget.channelLayout,
			m_audioParamTarget.fmt, m_audioParamTarget.freq,
			decodeChannelLayout, static_cast<AVSampleFormat>(audioFrame->frame->format),
			audioFrame->frame->sample_rate, 0, NULL);
		if (!m_swrContext || swr_init(m_swrContext) < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "Can't create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				audioFrame->frame->sample_rate,
				av_get_sample_fmt_name(static_cast<AVSampleFormat>(audioFrame->frame->format)),
				audioFrame->frame->channels, m_audioParamTarget.freq, av_get_sample_fmt_name(m_audioParamTarget.fmt),
				m_audioParamTarget.channels);
			swr_free(&m_swrContext);
			return -1;
		}
		// ʹ�� frame �еĲ������� is->audioSource
		m_audioParamSource.channelLayout = decodeChannelLayout;
		m_audioParamSource.channels = audioFrame->frame->channels;
		m_audioParamSource.freq = audioFrame->frame->sample_rate;
		m_audioParamSource.fmt = static_cast<AVSampleFormat>(audioFrame->frame->format);
	}

	if (m_swrContext)
	{
		const UINT8** in = const_cast<const UINT8**>(audioFrame->frame->extended_data);
		UINT8** out = &m_pAudioFrameRwrBuffer;
		INT32 outCount = static_cast<int64_t>(wantedNumberSamples * m_audioParamTarget.freq / audioFrame->frame->sample_rate + 256);
		INT32 outSize = av_samples_get_buffer_size(NULL, m_audioParamTarget.channels, outCount, m_audioParamTarget.fmt, 0);
		INT32 len2;
		if (outSize < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		av_fast_malloc(&m_pAudioFrameRwrBuffer, &m_audioFrameRwrSize, outSize);
		if (!m_pAudioFrameRwrBuffer)
			return AVERROR(ENOMEM);
		// ��Ƶ�ز���������ֵ���ز�����õ�����Ƶ�����е���������������
		len2 = swr_convert(m_swrContext, out, outCount, in, audioFrame->frame->nb_samples);
		if (len2 < 0)
		{
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(m_swrContext) < 0)
				swr_free(&m_swrContext);
		}
		m_pAudioFrame = m_pAudioFrameRwrBuffer;
		// �ز������ص�һ֡��Ƶ���ݴ�С
		resampledDataSize = len2 * m_audioParamTarget.channels * av_get_bytes_per_sample(m_audioParamTarget.fmt);
	}
	else
	{
		// δ���ز�������ָ��ָ��frame �е���Ƶ����
		m_pAudioFrame = audioFrame->frame->data[0];
		// ���� frame ��ָ�����Ƶ������ȡ��������С
		resampledDataSize = av_samples_get_buffer_size(NULL, audioFrame->frame->channels, audioFrame->frame->nb_samples,
			static_cast<AVSampleFormat>(audioFrame->frame->format), 1);;
	}

	if (!isnan(audioFrame->pts))
		m_audioClock = audioFrame->pts + static_cast<DOUBLE>(audioFrame->frame->nb_samples) / audioFrame->frame->sample_rate;
	else
		m_audioClock = NAN;
	return resampledDataSize;
}

BOOL Audio::OpenAudioPlaying()
{
	// �ļ��б�����Ƶ�ĸ�ʽ
	SDL_AudioSpec wantedSpec;
	// ��Ƶ�����豸��Ҫ�ĸ�ʽ
	SDL_AudioSpec actualSpec;
	// 2. ����Ƶ�豸��������Ƶ�����߳�
	// 2.1 ����Ƶ�豸����ȡ SDL �豸֧�ֵ���Ƶ���� actualSpec
	wantedSpec.freq = m_pAudioCodecContext->sample_rate; // ������( ÿ�������������)
	wantedSpec.format = AUDIO_S16SYS;  // ������ʽ  Signed 16-bit samples
	wantedSpec.channels = m_pAudioCodecContext->channels; // ����ͨ����
	wantedSpec.silence = 0; // ����ֵ
	wantedSpec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC)); // ÿ��ͨ����������
	wantedSpec.callback = SDLAudioCallback; // �ص�����
	wantedSpec.userdata = this; // �û�����
	if (!(m_audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantedSpec, &actualSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
	{
		av_log(NULL, AV_LOG_ERROR, "SDL_OpenAudio() failed: %s\n", SDL_GetError());
		return -1;
	}

	if (actualSpec.format != AUDIO_S16SYS)
	{
		av_log(NULL, AV_LOG_ERROR, "Audio Device not support S16SYS\n");
		return -1;
	}
	// 2.2 ���� SDL ��Ƶ����������Ƶ�ز�������
	m_audioParamTarget.fmt = AV_SAMPLE_FMT_S16; // Ŀ����Ƶ��ʽ
	m_audioParamTarget.freq = actualSpec.freq; // Ŀ����ƵƵ��
	// av_get_channel_layout_nb_channels  av_get_default_channel_layout
	m_audioParamTarget.channelLayout = av_get_default_channel_layout(actualSpec.channels); // Ŀ����Ƶ����
	m_audioParamTarget.channels = actualSpec.channels; // Ŀ����Ƶͨ����
	// Ŀ����Ƶ֡��С(channels*fmt(��Ӧ�ֽ���))
	m_audioParamTarget.frameSize = av_samples_get_buffer_size(NULL, actualSpec.channels, 1, m_audioParamTarget.fmt, 1);
	// ÿ����ֽ���
	m_audioParamTarget.bytesPerSec = av_samples_get_buffer_size(NULL, actualSpec.channels, actualSpec.freq, m_audioParamTarget.fmt, 1);
	if (m_audioParamTarget.bytesPerSec <= 0 || m_audioParamTarget.frameSize <= 0)
	{
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	m_audioParamSource = m_audioParamTarget;
	// ÿ֡���ֽ���
	m_audioHardwareBufferSize = actualSpec.size;
	m_audioFrameSize = 0;
	m_audioCopyIndex = 0;

	// 3. ��ͣ/ ������Ƶ�ص�����
	SDL_PauseAudioDevice(m_audioDevice, 0);
}

BOOL Audio::DecodeThread(void* arg)
{
	Audio* is = static_cast<Audio*>(arg);
	return is->OnDecodeThread();
}

void Audio::SDLAudioCallback(void* opaque, UINT8* stream, INT32 len)
{
	Audio* is = static_cast<Audio*>(opaque);
	return is->OnSDLAudioCallback(stream, len);
}
