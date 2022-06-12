#include "audio.h"
#include "packet.h"
#include "frame.h"

SDL_AudioDeviceID audioDevice;

// ��Ƶ����
static int AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	int ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			if (pPacketQueue->abortRequest)
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

// ��Ƶ�����߳�
static int AudioDecodeThread(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	AVFrame* pFrame = av_frame_alloc();
	Frame* audioFrame;

	int gotFrame = 0;
	AVRational timebase;
	int ret = 0;

	if (pFrame == NULL)
		return AVERROR(ENOMEM);

	while (1)
	{
		if (is->abortRequest)
			break;
		// ����
		gotFrame = AudioDecodeFrame(is->pAudioCodecContext, &is->audioPacketQueue, pFrame);
		if (gotFrame < 0)
			goto END;
		if (gotFrame)
		{
			timebase = AVRational{ 1, pFrame->sample_rate };
			if (!(audioFrame = FrameQueuePeekWritable(&is->audioFrameQueue)))
				goto END;
			// TODO ΪʲôҪ�ȳ��� timebase �ٳ��� timebase
			audioFrame->pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase);
			audioFrame->pos = pFrame->pkt_pos;
			// ��ǰ֡�����ģ�����������������/�����ʾ��ǵ�ǰ֡�Ĳ���ʱ��
			audioFrame->duration = av_q2d(AVRational{ pFrame->nb_samples, pFrame->sample_rate });
			// �� frame ���ݿ��� audioFrame->frame��audioFrame->frameָ����Ƶ frame ����β��
			av_frame_move_ref(audioFrame->frame, pFrame);
			// ������Ƶ frame ���д�С��дָ��
			FrameQueuePush(&is->audioFrameQueue);
		}
	}
END:
	av_frame_free(&pFrame);
	return ret;
}

// ����Ƶ��  
int OpenAudioStream(PlayerStation* is)
{
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	int ret;

	// 1.Ϊ��Ƶ������������ AVCodecContext
	// 1.1 ��ȡ���������� AVCodecParameters
	pCodecParameters = is->pAudioStream->codecpar;
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
	pCodecContext->pkt_timebase = is->pAudioStream->time_base;
	// ������Ƶ����������
	is->pAudioCodecContext = pCodecContext;

	// 2.������Ƶ�����߳�
	is->audioDecodeThreadID = SDL_CreateThread(AudioDecodeThread, "audio decode thread", is);
	return 0;
}

// ��Ƶ�ز���
static int AudioResample(PlayerStation* is, int64_t audioCallbackTime)
{
	int dataSize, resampledDataSize;
	int64_t decodeChannelLayout;
	int wantedNumberSamples;
	Frame* audioFrame;

	// ������û��֡��ȴ�
	while (FrameQueueNumberRemaining(&is->audioFrameQueue) == 0)
	{
		// �ȴ�ʱ��̫�������˳�
		if ((av_gettime_relative() - audioCallbackTime) > 1000000LL * is->audioHardwareBufferSize / is->audioParamTarget.bytesPerSec / 2)
			return -1;
		av_usleep(1000);
	}

	// ������ͷ���ɶ������� audioFrame ָ��ɶ�֡
	if (!(audioFrame = FrameQueuePeekReadable(&is->audioFrameQueue)))
		return -1;
	// ɾ����һ֡
	FrameQueueNext(&is->audioFrameQueue);

	// ���� frame ��ָ�����Ƶ������ȡ��������С
	dataSize = av_samples_get_buffer_size(NULL, audioFrame->frame->channels, audioFrame->frame->nb_samples,
		static_cast<AVSampleFormat>(audioFrame->frame->format), 1);

	// ��ȡ��������
	decodeChannelLayout = (audioFrame->frame->channel_layout &&
		audioFrame->frame->channels == av_get_channel_layout_nb_channels(audioFrame->frame->channel_layout)) ?
		audioFrame->frame->channel_layout : av_get_default_channel_layout(audioFrame->frame->channels);
	wantedNumberSamples = audioFrame->frame->nb_samples;

	// is->audioParamTarget �� SDL �ɽ��ܵ���Ƶ֡��
	if (audioFrame->frame->format != is->audioParamSource.fmt ||
		decodeChannelLayout != is->audioParamSource.channelLayout ||
		audioFrame->frame->sample_rate != is->audioParamSource.freq)
	{
		swr_free(&is->audioSwrContext);
		// �����ز�������
		is->audioSwrContext = swr_alloc_set_opts(NULL, is->audioParamTarget.channelLayout,
			is->audioParamTarget.fmt, is->audioParamTarget.freq,
			decodeChannelLayout, static_cast<AVSampleFormat>(audioFrame->frame->format),
			audioFrame->frame->sample_rate, 0, NULL);
		if (!is->audioSwrContext || swr_init(is->audioSwrContext) < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "Can't create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				audioFrame->frame->sample_rate,
				av_get_sample_fmt_name(static_cast<AVSampleFormat>(audioFrame->frame->format)),
				audioFrame->frame->channels, is->audioParamTarget.freq, av_get_sample_fmt_name(is->audioParamTarget.fmt),
				is->audioParamTarget.channels);
			swr_free(&is->audioSwrContext);
			return -1;
		}
		// ʹ�� frame �еĲ������� is->audioSource
		is->audioParamSource.channelLayout = decodeChannelLayout;
		is->audioParamSource.channels = audioFrame->frame->channels;
		is->audioParamSource.freq = audioFrame->frame->sample_rate;
		is->audioParamSource.fmt = static_cast<AVSampleFormat>(audioFrame->frame->format);
	}

	if (is->audioSwrContext)
	{
		const uint8_t** in = const_cast<const uint8_t**>(audioFrame->frame->extended_data);
		uint8_t** out = &is->pAudioFrameRwr;
		int outCount = static_cast<int64_t>(wantedNumberSamples * is->audioParamTarget.freq / audioFrame->frame->sample_rate + 256);
		int outSize = av_samples_get_buffer_size(NULL, is->audioParamTarget.channels, outCount, is->audioParamTarget.fmt, 0);
		int len2;
		if (outSize < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		av_fast_malloc(&is->pAudioFrameRwr, &is->audioFrameRwrSize, outSize);
		if (!is->pAudioFrameRwr)
			return AVERROR(ENOMEM);
		// ��Ƶ�ز���������ֵ���ز�����õ�����Ƶ�����е���������������
		len2 = swr_convert(is->audioSwrContext, out, outCount, in, audioFrame->frame->nb_samples);
		if (len2 < 0)
		{
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(is->audioSwrContext) < 0)
				swr_free(&is->audioSwrContext);
		}
		is->pAudioFrame = is->pAudioFrameRwr;
		// �ز������ص�һ֡��Ƶ���ݴ�С
		resampledDataSize = len2 * is->audioParamTarget.channels * av_get_bytes_per_sample(is->audioParamTarget.fmt);
	}
	else
	{
		// δ���ز�������ָ��ָ��frame �е���Ƶ����
		is->pAudioFrame = audioFrame->frame->data[0];
		resampledDataSize = dataSize;
	}

	if (!isnan(audioFrame->pts))
		is->audioClock = audioFrame->pts + static_cast<double>(audioFrame->frame->nb_samples) / audioFrame->frame->sample_rate;
	else
		is->audioClock = NAN;
	is->audioClockSerial = audioFrame->serial;
	return resampledDataSize;
}

// ����Ƶ��Ⱦ
static int OpenAudioPlaying(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	// �ļ��б�����Ƶ�ĸ�ʽ
	SDL_AudioSpec wantedSpec;
	// ��Ƶ�����豸��Ҫ�ĸ�ʽ
	SDL_AudioSpec actualSpec;
	// 2. ����Ƶ�豸��������Ƶ�����߳�
	// 2.1 ����Ƶ�豸����ȡ SDL �豸֧�ֵ���Ƶ���� actualSpec
	wantedSpec.freq = is->pAudioCodecContext->sample_rate; // ������( ÿ�������������)
	wantedSpec.format = AUDIO_S16SYS;  // ������ʽ  Signed 16-bit samples
	wantedSpec.channels = is->pAudioCodecContext->channels; // ����ͨ����
	wantedSpec.silence = 0; // ����ֵ
	wantedSpec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC)); // ÿ��ͨ����������
	wantedSpec.callback = SDLAudioCallback; // �ص�����
	wantedSpec.userdata = is; // �û�����
	if (!(audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantedSpec, &actualSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
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
	is->audioParamTarget.fmt = AV_SAMPLE_FMT_S16; // Ŀ����Ƶ��ʽ
	is->audioParamTarget.freq = actualSpec.freq; // Ŀ����ƵƵ��
	// av_get_channel_layout_nb_channels  av_get_default_channel_layout
	is->audioParamTarget.channelLayout = av_get_default_channel_layout(actualSpec.channels); // Ŀ����Ƶ����
	is->audioParamTarget.channels = actualSpec.channels; // Ŀ����Ƶͨ����
	// Ŀ����Ƶ֡��С(channels*fmt(��Ӧ�ֽ���))
	is->audioParamTarget.frameSize = av_samples_get_buffer_size(NULL, actualSpec.channels, 1, is->audioParamTarget.fmt, 1);
	// ÿ����ֽ���
	is->audioParamTarget.bytesPerSec = av_samples_get_buffer_size(NULL, actualSpec.channels, actualSpec.freq, is->audioParamTarget.fmt, 1);
	if (is->audioParamTarget.bytesPerSec <= 0 || is->audioParamTarget.frameSize <= 0)
	{
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	is->audioParamSource = is->audioParamTarget;
	// ÿ֡���ֽ���
	is->audioHardwareBufferSize = actualSpec.size;
	is->audioFrameSize = 0;
	is->audioCopyIndex = 0;

	// 3. ��ͣ/ ������Ƶ�ص�����
	SDL_PauseAudioDevice(audioDevice, 0);
}

// ��Ƶ�ص�����
static void SDLAudioCallback(void* opaque, Uint8* stream, int len)
{
	PlayerStation* is = static_cast<PlayerStation*>(opaque);
	int audioSize, len1;
	// �ص���ʼ��ʱ��
	int64_t audioCallbackTime = av_gettime_relative();

	// TODO ��Ƶ��ͣҲ��������
	if (is->paused)
	{
		memset(stream, 0, len);
		return;
	}
	while (len > 0)
	{
		if (is->audioCopyIndex >= static_cast<int>(is->audioFrameSize))
		{
			// 1.����Ƶ frame ������ȡ��һ�� frame��ת��Ϊ��Ƶ�豸֧�ֵĸ�ʽ������ֵ���ز�����Ƶ֡�Ĵ�С
			audioSize = AudioResample(is, audioCallbackTime);
			if (audioSize < 0)
			{
				is->pAudioFrame = NULL;
				is->audioFrameSize = SDL_AUDIO_MIN_BUFFER_SIZE / is->audioParamTarget.frameSize * is->audioParamTarget.frameSize;
			}
			else
				is->audioFrameSize = audioSize;
			is->audioCopyIndex = 0;
		}
		// ���� is->audioCopyIndex �����ã���ֹһ֡��Ƶ���ݴ�С���� SDL ��Ƶ��������С������һ֡������Ҫ������ο���
		// �� is->audioCopyIndex ��ʶ�ز���֡���ѿ��� SDL ��Ƶ������������λ�������� len1 ��ʶ���ο�����������
		len1 = is->audioFrameSize - is->audioCopyIndex;
		if (len1 > len)
			len1 = len;
		// 2.��ת�������Ƶ���ݿ�������Ƶ������ stream �У�֮��Ĳ��ž�����Ƶ�豸����������
		if (is->pAudioFrame != NULL)
			memcpy(stream, static_cast<uint8_t*>(is->pAudioFrame) + is->audioCopyIndex, len1);
		else
			memset(stream, 0, len1);
		len -= len1;
		stream += len1;
		is->audioCopyIndex += len1;
	}

	is->audioWriteBufferSize = is->audioFrameSize - is->audioCopyIndex;

	// 3.����ʱ��
	if (!isnan(is->audioClock))
	{
		// ������Ƶʱ�ӣ�����ʱ�̣�ÿ���������������������ݺ�
		// ǰ�� audioDecodeFrame �и��µ� is->audioClock ������Ƶ֡Ϊ��λ�����Դ˴��ڶ�������Ҫ��ȥδ����������ռ�õ�ʱ��
		SetClockAt(&is->audioPlayClock, is->audioClock - static_cast<double>(2 * is->audioHardwareBufferSize + is->audioWriteBufferSize) /
			is->audioParamTarget.bytesPerSec, is->audioClockSerial, audioCallbackTime / 1000000.0);
	}
}

// ����Ƶ��Ⱦ
int OpenAudio(PlayerStation* is)
{
	// ������Ƶ����������(����ʱ���), ������Ƶ�����߳�
	OpenAudioStream(is);
	// ����Ƶ�����豸�������ûص�����
	OpenAudioPlaying(is);
	return 0;
}
