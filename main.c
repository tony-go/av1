#include <SDL2/SDL.h>
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#define WIDTH 640
#define HEIGHT 480
#define FPS 30

int main() {
  avdevice_register_all();

  AVFormatContext *input_ctx = NULL;
  const AVInputFormat *input_fmt = av_find_input_format("avfoundation");
  AVDictionary *options = NULL;
  av_dict_set(&options, "framerate", "30", 0);
  av_dict_set(&options, "video_size", "640x480", 0);
  av_dict_set(&options, "pixel_format", "uyvy422", 0);
  assert(avformat_open_input(&input_ctx, "0", input_fmt, &options) == 0);
  assert(avformat_find_stream_info(input_ctx, NULL) >= 0);
  int video_stream_index =
      av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVStream *in_stream = input_ctx->streams[video_stream_index];
  AVCodecParameters *in_params = in_stream->codecpar;

  const AVCodec *decoder = avcodec_find_decoder(in_params->codec_id);
  AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
  avcodec_parameters_to_context(dec_ctx, in_params);
  avcodec_open2(dec_ctx, decoder, NULL);

  struct SwsContext *sws =
      sws_getContext(WIDTH, HEIGHT, dec_ctx->pix_fmt, WIDTH, HEIGHT,
                     AV_PIX_FMT_UYVY422, SWS_BILINEAR, NULL, NULL, NULL);

  assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0);
  SDL_Window *window =
      SDL_CreateWindow("Live Camera", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_UYVY,
                        SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  AVFrame *rgb_frame = av_frame_alloc();
  int rgb_bufsize =
      av_image_get_buffer_size(AV_PIX_FMT_UYVY422, WIDTH, HEIGHT, 1);
  uint8_t *rgb_buf = (uint8_t *)av_malloc(rgb_bufsize);
  av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buf,
                       AV_PIX_FMT_UYVY422, WIDTH, HEIGHT, 1);

  // === Main loop ===
  SDL_Event event;
  while (1) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        goto quit;
    }

    if (av_read_frame(input_ctx, pkt) < 0) {
      av_packet_unref(pkt);
      continue;
    }
    if (pkt->stream_index != video_stream_index) {
      av_packet_unref(pkt);
      continue;
    }

    avcodec_send_packet(dec_ctx, pkt);
    if (avcodec_receive_frame(dec_ctx, frame) == 0) {
      sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0,
                HEIGHT, rgb_frame->data, rgb_frame->linesize);

      SDL_UpdateTexture(texture, NULL, rgb_frame->data[0],
                        rgb_frame->linesize[0]);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, NULL, NULL);
      SDL_RenderPresent(renderer);
    }
    av_packet_unref(pkt);
    // SDL_Delay(1000 / FPS);
  }

quit:
  av_free(rgb_buf);
  av_frame_free(&frame);
  av_frame_free(&rgb_frame);
  av_packet_free(&pkt);
  sws_freeContext(sws);
  avcodec_free_context(&dec_ctx);
  avformat_close_input(&input_ctx);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
