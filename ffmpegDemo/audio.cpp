#include "audio.h"
#include "packet.h"
#include "frame.h"

Audio::Audio()
{
	// -DSS TODO ��ʼ������
}

BOOL Audio::Open()
{
	// ������Ƶ����������(����ʱ���), ������Ƶ�����߳�
	OpenAudioStream();
	// ����Ƶ�����豸�������ûص�����
	OpenAudioPlaying();
	return TRUE;

}

void Audio::Close()
{

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
		if (m_stop)
			break;
		// ����
		receive = AudioDecodeFrame(m_pAudioCodecContext, &m_packetQueue, pFrame);
		if (!receive)
			goto END;

		timebase = AVRational{ 1, pFrame->sample_rate };
		if (!(audioFrame = FrameQueuePeekWritable(&m_frameQueue)))
			goto END;
		// TODO ΪʲôҪ�ȳ��� timebase �ٳ��� timebase
		audioFrame->pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase);
		audioFrame->pos = pFrame->pkt_pos;
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

int Audio::AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	int ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			if (m_stop)
				return 1;
			// 3.2 һ����Ƶ packet ��һ����� frame��ÿ�� avcodec_receive_frame() ����һ�� frame
			ret = avcodec_receive_frame(pCodecContext, frame);
			if (ret >= 0)
			{
				// ʱ���任���� d->avcontex->packetTimebase ʱ��ת���� 1/frame->sampleRate ʱ��
				AVRational timebase = AVRational{ 1, frame->sample_rate };
				if (frame->pts != AV_NOPTS_VALUE)
					// pts ����ת��
					frame->pts = av_rescale_q(frame->pts, pCodecContext->pkt_timebase, timebase);
				else
					av_log(NULL, AV_LOG_WARNING, "frame->pts no\n");
				return 1;
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
		if (PacketQueueGet(pPacketQueue, &pkt, true) < 0)
			return -1;
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
}

BOOL Audio::OpenAudioStream()
{
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	int ret;

	// 1.Ϊ��Ƶ������������ AVCodecContext
	// 1.1 ��ȡ���������� AVCodecParameters
	pCodecParameters = m_pAudioStream->codecpar;
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
	pCodecContext->pkt_timebase = m_pAudioStream->time_base;
	// ������Ƶ����������
	m_pAudioCodecContext = pCodecContext;

	// 2.������Ƶ�����߳�
	m_decodeThread = SDL_CreateThread(AudioDecodeThread, "audio decode thread", this);
	return 0;
}

BOOL Audio::AudioResample(INT64 callbackTime)
{
	int dataSize, resampledDataSize;
	int64_t decodeChannelLayout;
	int wantedNumberSamples;
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

	// ���� frame ��ָ�����Ƶ������ȡ��������С
	dataSize = av_samples_get_buffer_size(NULL, audioFrame->frame->channels, audioFrame->frame->nb_samples,
		static_cast<AVSampleFormat>(audioFrame->frame->format), 1);

	// ��ȡ��������
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
		const uint8_t** in = const_cast<const uint8_t**>(audioFrame->frame->extended_data);
		uint8_t** out = &m_pAudioFrameRwr;
		int outCount = static_cast<int64_t>(wantedNumberSamples * m_audioParamTarget.freq / audioFrame->frame->sample_rate + 256);
		int outSize = av_samples_get_buffer_size(NULL, m_audioParamTarget.channels, outCount, m_audioParamTarget.fmt, 0);
		int len2;
		if (outSize < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		av_fast_malloc(&m_pAudioFrameRwr, &m_audioFrameRwrSize, outSize);
		if (!m_pAudioFrameRwr)
			return AVERROR(ENOMEM);
		// ��Ƶ�ز���������ֵ���ز�����õ�����Ƶ�����е���������������
		len2 = swr_convert(m_swrContext, out, outCount, in, audioFrame->frame->nb_samples);
		if (len2 < 0)
		{
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(m_swrContext) < 0)
				swr_free(&m_swrContext);
		}
		m_pAudioFrame = m_pAudioFrameRwr;
		// �ز������ص�һ֡��Ƶ���ݴ�С
		resampledDataSize = len2 * m_audioParamTarget.channels * av_get_bytes_per_sample(m_audioParamTarget.fmt);
	}
	else
	{
		// δ���ز�������ָ��ָ��frame �е���Ƶ����
		m_pAudioFrame = audioFrame->frame->data[0];
		resampledDataSize = dataSize;
	}

	if (!isnan(audioFrame->pts))
		m_audioClock = audioFrame->pts + static_cast<double>(audioFrame->frame->nb_samples) / audioFrame->frame->sample_rate;
	else
		m_audioClock = NAN;
	m_audioClockSerial = audioFrame->serial;
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
	is->OnDecodeThread();
}
