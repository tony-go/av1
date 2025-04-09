#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

// WAV header structure
typedef struct {
    uint8_t riff[4];        // "RIFF"
    uint32_t file_size;     // File size - 8
    uint8_t wave[4];        // "WAVE"
    uint8_t fmt[4];         // "fmt "
    uint32_t fmt_size;      // Format chunk size (16)
    uint16_t audio_format;  // Audio format (1 for PCM)
    uint16_t num_channels;  // Number of channels
    uint32_t sample_rate;   // Sample rate
    uint32_t byte_rate;     // Byte rate
    uint16_t block_align;   // Block align
    uint16_t bits_per_sample; // Bits per sample
    uint8_t data[4];        // "data"
    uint32_t data_size;     // Data chunk size
} WavHeader;

void write_wav_header(FILE *file, int sample_rate, int channels, int bits_per_sample, int data_size) {
    WavHeader header = {0};
    
    // RIFF header
    memcpy(header.riff, "RIFF", 4);
    header.file_size = data_size + sizeof(WavHeader) - 8;
    memcpy(header.wave, "WAVE", 4);
    
    // Format chunk
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1; // PCM
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = bits_per_sample;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    
    // Data chunk
    memcpy(header.data, "data", 4);
    header.data_size = data_size;
    
    fwrite(&header, 1, sizeof(WavHeader), file);
}

void print_stream_info(AVFormatContext *fmt_ctx) {
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *stream = fmt_ctx->streams[i];
        AVCodecParameters *codec_params = stream->codecpar;
        
        printf("Stream %d:\n", i);
        printf("  Type: %s\n", av_get_media_type_string(codec_params->codec_type));
        printf("  Codec: %s\n", avcodec_get_name(codec_params->codec_id));
        printf("  Sample rate: %d\n", codec_params->sample_rate);
        printf("  Channel layout: %" PRIu64 "\n", codec_params->ch_layout.u.mask);
        printf("  Format: %s\n", av_get_sample_fmt_name(codec_params->format));
        printf("  Bits per sample: %d\n", av_get_bytes_per_sample(codec_params->format) * 8);
    }
}

// Convert float samples to 16-bit PCM with proper scaling
void convert_float_to_pcm16(const float *input, int16_t *output, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        // Scale the float value to the 16-bit range and clamp
        float sample = input[i];
        sample = fmaxf(-1.0f, fminf(1.0f, sample));  // Clamp to [-1, 1]
        output[i] = (int16_t)(sample * 32767.0f);    // Scale to 16-bit range
    }
}

int main() {
    int ret;
    AVFormatContext *fmt_ctx = NULL;
    AVPacket *pkt = NULL;
    const char *device_name = ":2";
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

    // Open input device
    AVDictionary *options = NULL;
    av_dict_set(&options, "channels", "1", 0);  // Set to mono since that's what we're getting

    const AVInputFormat *input_format = av_find_input_format("avfoundation");
    if (!input_format) {
        fprintf(stderr, "Could not find avfoundation input format\n");
        av_packet_free(&pkt);
        avformat_free_context(fmt_ctx);
        return 1;
    }

    ret = avformat_open_input(&fmt_ctx, device_name, input_format, &options);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Could not open input device: %s\n", errbuf);
        av_packet_free(&pkt);
        avformat_free_context(fmt_ctx);
        return 1;
    }

    // Get stream information
    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        av_packet_free(&pkt);
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
        return 1;
    }

    // Print stream information
    print_stream_info(fmt_ctx);

    // Open output file
    FILE *output_file = fopen("output.wav", "wb");
    if (!output_file) {
        fprintf(stderr, "Could not open output file\n");
        av_packet_free(&pkt);
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
        return 1;
    }

    // Get the first audio stream
    int audio_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        fprintf(stderr, "Could not find audio stream\n");
        fclose(output_file);
        av_packet_free(&pkt);
        avformat_close_input(&fmt_ctx);
        avformat_free_context(fmt_ctx);
        return 1;
    }

    AVCodecParameters *codec_params = fmt_ctx->streams[audio_stream_index]->codecpar;
    int sample_rate = codec_params->sample_rate;
    int channels = 1;  // We know it's mono from the device info
    int bits_per_sample = 16;  // We'll convert to 16-bit PCM

    // Write temporary WAV header (we'll update the size later)
    write_wav_header(output_file, sample_rate, channels, bits_per_sample, 0);

    printf("Recording for %d seconds...\n", RECORD_SECONDS);
    start_time = time(NULL);

    // Allocate buffer for converted samples
    int16_t *converted_samples = NULL;
    size_t max_samples = 0;

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
                // No frame available yet, continue
                continue;
            }
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error reading frame: %s\n", errbuf);
            break;
        }

        // Only process audio frames
        if (pkt->stream_index == audio_stream_index) {
            int num_samples = pkt->size / sizeof(float);
            
            // Allocate or reallocate buffer if needed
            if (num_samples > max_samples) {
                free(converted_samples);
                converted_samples = malloc(num_samples * sizeof(int16_t));
                max_samples = num_samples;
            }

            // Convert float samples to 16-bit PCM
            convert_float_to_pcm16((float *)pkt->data, converted_samples, num_samples);

            // Write the converted audio data to file
            fwrite(converted_samples, sizeof(int16_t), num_samples, output_file);
            total_bytes += num_samples * sizeof(int16_t);
            
            printf("\rRecording: %d seconds remaining...", 
                   RECORD_SECONDS - (int)difftime(current_time, start_time));
            fflush(stdout);
            frame_count++;
        }

        av_packet_unref(pkt);
    }

    // Free the conversion buffer
    free(converted_samples);

    // Update WAV header with correct file size
    fseek(output_file, 0, SEEK_SET);
    write_wav_header(output_file, sample_rate, channels, bits_per_sample, total_bytes);

    // Cleanup
    fclose(output_file);
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
    avformat_free_context(fmt_ctx);

    printf("\nRecorded %d frames to output.wav\n", frame_count);
    return 0;
}
