#include <SDL2/SDL.h>
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/_pthread/_pthread_cond_t.h>
#include <sys/_pthread/_pthread_mutex_t.h>
#include <unistd.h>

#define WIDTH 640
#define HEIGHT 480
#define FPS 30
#define FRAME_QUEUE_SIZE 8

volatile int running = 1;

typedef struct {
  AVPacket *packets[FRAME_QUEUE_SIZE];
  int head;
  int tail;
  int count;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} FrameQueue;

FrameQueue queue = {.head = 0,
                    .tail = 0,
                    .count = 0,
                    .mutex = PTHREAD_MUTEX_INITIALIZER,
                    .cond = PTHREAD_COND_INITIALIZER};

void push_packet(AVPacket *packet) {
  pthread_mutex_lock(&queue.mutex);
  while (queue.count == FRAME_QUEUE_SIZE && running)
    pthread_cond_wait(&queue.cond, &queue.mutex);

  if (!running) {
    pthread_mutex_unlock(&queue.mutex);
    return;
  }

  queue.packets[queue.tail] = packet;
  queue.tail = (queue.tail + 1) % FRAME_QUEUE_SIZE;
  queue.count++;

  pthread_cond_signal(&queue.cond);
  pthread_mutex_unlock(&queue.mutex);
}

AVPacket *pop_packet(void) {
  pthread_mutex_lock(&queue.mutex);
  while (queue.count == 0 && running)
    pthread_cond_wait(&queue.cond, &queue.mutex);

  if (!running) {
    pthread_mutex_unlock(&queue.mutex);
    return NULL;
  }

  AVPacket *packet = queue.packets[queue.head];
  queue.head = (queue.head + 1) % FRAME_QUEUE_SIZE;
  queue.count--;

  pthread_cond_signal(&queue.cond);
  pthread_mutex_unlock(&queue.mutex);
  return packet;
}
void *capture_thread(void *arg) {
  AVFormatContext *input_ctx = (AVFormatContext *)arg;
  int video_stream_index =
      av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  AVCodecParameters *in_params =
      input_ctx->streams[video_stream_index]->codecpar;

  // === Decode raw camera packets ===
  const AVCodec *raw_decoder = avcodec_find_decoder(in_params->codec_id);
  AVCodecContext *raw_dec_ctx = avcodec_alloc_context3(raw_decoder);
  avcodec_parameters_to_context(raw_dec_ctx, in_params);
  avcodec_open2(raw_dec_ctx, raw_decoder, NULL);

  // === AV1 encoder ===
  const AVCodec *enc = avcodec_find_encoder_by_name("libsvtav1");
  AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);
  enc_ctx->width = WIDTH;
  enc_ctx->height = HEIGHT;
  enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  enc_ctx->time_base = (AVRational){1, FPS};
  enc_ctx->framerate = (AVRational){FPS, 1};
  enc_ctx->thread_count = 4;
  enc_ctx->pkt_timebase = enc_ctx->time_base;
  enc_ctx->gop_size = 1; // Every frame = keyframe
  assert(avcodec_open2(enc_ctx, enc, NULL) == 0);

  // === Swscale contexts ===
  struct SwsContext *to_yuv =
      sws_getContext(WIDTH, HEIGHT, in_params->format, WIDTH, HEIGHT,
                     AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
  struct SwsContext *to_rgb =
      sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_YUV420P, WIDTH, HEIGHT,
                     AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

  AVPacket *pkt = av_packet_alloc();
  AVFrame *cam = av_frame_alloc();
  AVFrame *yuv = av_frame_alloc();
  AVFrame *rgb = av_frame_alloc();

  yuv->format = AV_PIX_FMT_YUV420P;
  yuv->width = WIDTH;
  yuv->height = HEIGHT;
  av_frame_get_buffer(yuv, 32);

  rgb->format = AV_PIX_FMT_RGB24;
  rgb->width = WIDTH;
  rgb->height = HEIGHT;
  av_frame_get_buffer(rgb, 32);

  int i = 0;
  while (running) {
    int ret = av_read_frame(input_ctx, pkt);
    if (ret == AVERROR(EAGAIN))
      continue;
    else if (ret < 0)
      break;

    if (pkt->stream_index != video_stream_index) {
      av_packet_unref(pkt);
      continue;
    }

    avcodec_send_packet(raw_dec_ctx, pkt);
    av_packet_unref(pkt);
    while (avcodec_receive_frame(raw_dec_ctx, cam) == 0) {
      sws_scale(to_yuv, (const uint8_t *const *)cam->data, cam->linesize, 0,
                HEIGHT, yuv->data, yuv->linesize);
      yuv->pts = i++;
      avcodec_send_frame(enc_ctx, yuv);
      while (avcodec_receive_packet(enc_ctx, pkt) == 0) {
        AVPacket *out = av_packet_alloc();
        av_packet_ref(out, pkt);
        push_packet(out);
        av_packet_unref(pkt);
      }
    }
  }

  av_frame_free(&cam);
  av_frame_free(&yuv);
  av_frame_free(&rgb);
  av_packet_free(&pkt);
  sws_freeContext(to_yuv);
  sws_freeContext(to_rgb);
  avcodec_free_context(&raw_dec_ctx);
  avcodec_free_context(&enc_ctx);
  // avcodec_free_context(&dec_ctx);
  avformat_close_input(&input_ctx);
  return NULL;
}

int main() {
  avdevice_register_all();
  SDL_Init(SDL_INIT_VIDEO);

  // === Capture from AVFoundation ===
  AVFormatContext *input_ctx = NULL;
  const AVInputFormat *input_fmt = av_find_input_format("avfoundation");
  AVDictionary *options = NULL;
  av_dict_set(&options, "framerate", "30", 0);
  av_dict_set(&options, "video_size", "640x480", 0);
  av_dict_set(&options, "pixel_format", "uyvy422", 0);
  assert(avformat_open_input(&input_ctx, "0", input_fmt, &options) == 0);
  assert(avformat_find_stream_info(input_ctx, NULL) >= 0);

  // === SDL setup ===
  SDL_Window *win =
      SDL_CreateWindow("AV1 Live Loopback", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(
      ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

  struct SwsContext *to_rgb =
      sws_getContext(WIDTH, HEIGHT, AV_PIX_FMT_YUV420P, WIDTH, HEIGHT,
                     AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

  AVFrame *rgb = av_frame_alloc();
  rgb->format = AV_PIX_FMT_RGB24;
  rgb->width = WIDTH;
  rgb->height = HEIGHT;
  av_frame_get_buffer(rgb, 32);

  // === AV1 decoder ===
  const AVCodec *dec = avcodec_find_decoder_by_name("libaom-av1");
  AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
  assert(avcodec_open2(dec_ctx, dec, NULL) == 0);

  pthread_t thread;
  pthread_create(&thread, NULL, capture_thread, input_ctx);

  SDL_Event e;
  while (running) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = 0;
        av_read_pause(input_ctx);
      }
    }
    AVPacket *packet = pop_packet();
    if (!packet)
      continue;

    avcodec_send_packet(dec_ctx, packet);
    av_packet_free(&packet);

    AVFrame *decoded = av_frame_alloc();
    while (avcodec_receive_frame(dec_ctx, decoded) == 0) {
      sws_scale(to_rgb, (const uint8_t *const *)decoded->data,
                decoded->linesize, 0, HEIGHT, rgb->data, rgb->linesize);

      SDL_UpdateTexture(tex, NULL, rgb->data[0], rgb->linesize[0]);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, NULL, NULL);
      SDL_RenderPresent(ren);
    }

    av_frame_free(&decoded);
  }

  pthread_join(thread, NULL);

  av_frame_free(&rgb);
  sws_freeContext(to_rgb);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
