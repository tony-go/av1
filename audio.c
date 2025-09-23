
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>

int main(int argc, char **argv) {
  int err;

  avdevice_register_all();

  AVFormatContext *input_ctx = NULL;
  const AVInputFormat *input_fmt = av_find_input_format("avfoundation");
  if (input_fmt == NULL) {
    printf("av_find_input_format did not worked\n");
    return 1;
  }

  AVDictionary *options = NULL;
  err = avformat_open_input(&input_ctx, ":0", input_fmt, &options);
  assert(err == 0);

  err = avformat_find_stream_info(input_ctx, NULL);
  assert(err >= 0);

  int audio_stream_index =
      av_find_best_stream(input_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  printf("audio_stream_index %i\n", audio_stream_index);

  AVStream *audio_stream = input_ctx->streams[audio_stream_index];
  assert(audio_stream);

  // Setup Input codec

  AVCodecParameters *audio_codec_params = audio_stream->codecpar;
  assert(audio_codec_params);

  const AVCodec *input_codec =
      avcodec_find_decoder(audio_codec_params->codec_id);
  assert(input_codec);

  AVCodecContext *input_codec_ctx = avcodec_alloc_context3(input_codec);
  assert(input_codec_ctx);

  err = avcodec_parameters_to_context(input_codec_ctx, audio_codec_params);
  assert(err >= 0);

  err = avcodec_open2(input_codec_ctx, input_codec, NULL);
  assert(err >= 0);

  return 0;
}
