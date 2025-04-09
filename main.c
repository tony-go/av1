#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main() {
  int ret;
  AVFormatContext *fmt_ctx = NULL;
  AVPacket *pkt = NULL;
  AVFrame *frame = NULL;
  SwrContext *swr_ctx = NULL;
  const char *device_name = ":0";
  const int RECORD_SECONDS = 5;
  time_t start_time, current_time;
  int total_bytes = 0;

  // Register all devices
  avdevice_register_all();

  // Allocate format context
  fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx) {
    fprintf(stderr, "Could not allocate format context\n");
    return 1;
  }

  // Allocate packet
  pkt = av_packet_alloc();
  if (!pkt) {
    fprintf(stderr, "Could not allocate packet\n");
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Allocate frame
  frame = av_frame_alloc();
  if (!frame) {
    fprintf(stderr, "Could not allocate frame\n");
    av_packet_free(&pkt);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Open input device
  AVDictionary *options = NULL;
  av_dict_set(&options, "channels", "1",
              0); // Set to mono since that's what we're getting

  const AVInputFormat *input_format = av_find_input_format("avfoundation");
  if (!input_format) {
    fprintf(stderr, "Could not find avfoundation input format\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  ret = avformat_open_input(&fmt_ctx, device_name, input_format, &options);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    fprintf(stderr, "Could not open input device: %s\n", errbuf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Get stream information
  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not find stream information\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Find audio stream
  int audio_stream_index = -1;
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream_index = i;
      break;
    }
  }

  if (audio_stream_index == -1) {
    fprintf(stderr, "Could not find audio stream\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Get codec parameters
  AVCodecParameters *codec_params =
      fmt_ctx->streams[audio_stream_index]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    fprintf(stderr, "Could not find codec\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Allocate codec context
  AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    fprintf(stderr, "Could not allocate codec context\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Copy codec parameters to codec context
  ret = avcodec_parameters_to_context(codec_ctx, codec_params);
  if (ret < 0) {
    fprintf(stderr, "Could not copy codec parameters\n");
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Open codec
  ret = avcodec_open2(codec_ctx, codec, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open codec\n");
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Set up resampler
  swr_ctx = swr_alloc();
  if (!swr_ctx) {
    fprintf(stderr, "Could not allocate resampler context\n");
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Configure resampler with proper channel layouts
  AVChannelLayout in_ch_layout = {0};
  AVChannelLayout out_ch_layout = {0};

  // Get input channel layout from codec context
  av_channel_layout_copy(&in_ch_layout, &codec_ctx->ch_layout);

  // Set output channel layout to mono
  av_channel_layout_default(&out_ch_layout, 1);

  // Configure resampler with proper channel layouts
  ret = swr_alloc_set_opts2(&swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_S16, 48000,
                            &in_ch_layout, codec_ctx->sample_fmt,
                            codec_ctx->sample_rate, 0, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not set resampler options\n");
    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  ret = swr_init(swr_ctx);
  if (ret < 0) {
    fprintf(stderr, "Could not initialize resampler\n");
    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Open output file
  FILE *output_file = fopen("output.wav", "wb");
  if (!output_file) {
    fprintf(stderr, "Could not open output file\n");
    av_channel_layout_uninit(&in_ch_layout);
    av_channel_layout_uninit(&out_ch_layout);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);
    return 1;
  }

  // Write WAV header
  uint8_t header[44] = {0};
  memcpy(header, "RIFF", 4);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  *(uint32_t *)(header + 16) = 16;        // fmt chunk size
  *(uint16_t *)(header + 20) = 1;         // PCM format
  *(uint16_t *)(header + 22) = 1;         // mono
  *(uint32_t *)(header + 24) = 48000;     // sample rate
  *(uint32_t *)(header + 28) = 48000 * 2; // byte rate
  *(uint16_t *)(header + 32) = 2;         // block align
  *(uint16_t *)(header + 34) = 16;        // bits per sample
  memcpy(header + 36, "data", 4);
  fwrite(header, 1, 44, output_file);

  printf("Recording for %d seconds...\n", RECORD_SECONDS);
  start_time = time(NULL);

  // Allocate buffer for converted samples
  uint8_t *converted_data = NULL;
  int converted_linesize;
  int max_samples = 0;

  // Read frames for 5 seconds
  int frame_count = 0;
  while (1) {
    current_time = time(NULL);
    if (difftime(current_time, start_time) >= RECORD_SECONDS) {
      printf("\nRecording complete!\n");
      break;
    }

    ret = av_read_frame(fmt_ctx, pkt);
    if (ret < 0) {
      if (ret == AVERROR(EAGAIN)) {
        continue;
      }
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      fprintf(stderr, "Error reading frame: %s\n", errbuf);
      break;
    }

    if (pkt->stream_index == audio_stream_index) {
      ret = avcodec_send_packet(codec_ctx, pkt);
      if (ret < 0) {
        fprintf(stderr, "Error sending packet to decoder\n");
        break;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
          fprintf(stderr, "Error receiving frame from decoder\n");
          break;
        }

        // Calculate number of output samples
        int out_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
            48000, frame->sample_rate, AV_ROUND_UP);

        // Allocate or reallocate buffer if needed
        if (out_samples > max_samples) {
          av_freep(&converted_data);
          ret = av_samples_alloc(&converted_data, &converted_linesize, 1,
                                 out_samples, AV_SAMPLE_FMT_S16, 0);
          if (ret < 0) {
            fprintf(stderr, "Could not allocate samples\n");
            break;
          }
          max_samples = out_samples;
        }

        // Convert samples
        ret = swr_convert(swr_ctx, &converted_data, out_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
          fprintf(stderr, "Error converting samples\n");
          break;
        }

        // Write converted samples to file
        fwrite(converted_data, 1, ret * 2, output_file);
        total_bytes += ret * 2;

        printf("\rRecording: %d seconds remaining...",
               RECORD_SECONDS - (int)difftime(current_time, start_time));
        fflush(stdout);
        frame_count++;
      }
    }

    av_packet_unref(pkt);
  }

  // Update WAV header with correct file size
  fseek(output_file, 4, SEEK_SET);
  uint32_t file_size = total_bytes + 36;
  fwrite(&file_size, 1, 4, output_file);
  fseek(output_file, 40, SEEK_SET);
  fwrite(&total_bytes, 1, 4, output_file);

  // Cleanup
  av_freep(&converted_data);
  av_channel_layout_uninit(&in_ch_layout);
  av_channel_layout_uninit(&out_ch_layout);
  swr_free(&swr_ctx);
  avcodec_free_context(&codec_ctx);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avformat_close_input(&fmt_ctx);
  avformat_free_context(fmt_ctx);
  fclose(output_file);

  printf("\nRecorded %d frames to output.wav\n", frame_count);
  return 0;
}
