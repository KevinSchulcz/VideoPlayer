// export PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH
// gcc $(pkg-config --cflags gtk4 libavformat libavcodec libavutil libswscale) -o VideoPlayer VideoPlayer.c $(pkg-config --libs gtk4 libavformat libavcodec libavutil libswscale)

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define BUFFER_SIZE 10

typedef struct {
    AVFrame *frame;
    int is_full;
} CircularBuffer;

CircularBuffer buffer[BUFFER_SIZE];
int read_index = 0, write_index = 0;
pthread_mutex_t buffer_mutex;
pthread_cond_t buffer_full_cond, buffer_empty_cond;
static pthread_t decoder_thread;

GtkWidget *window;
GtkWidget *image;
AVFormatContext *fmt_ctx;
AVCodecContext *codec_ctx;
const AVCodec *codec;
struct SwsContext *sws_ctx;
int frame_rate = 30;
GMainLoop *main_loop;
uint8_t *rgb_buffer = NULL;

void *decode_thread(void *arg) {
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    
    // Initialize software scaler
    sws_ctx = sws_getContext(
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    
    // Allocate RGB buffer
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    rgb_buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer,
                         AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == 0) {  // Assuming video stream index is 0
            int response = avcodec_send_packet(codec_ctx, packet);
            if (response < 0) {
                fprintf(stderr, "Error sending packet to decoder\n");
                continue;
            }

            response = avcodec_receive_frame(codec_ctx, frame);
            if (response >= 0) {
                // Convert the image from its native format to RGB
                sws_scale(sws_ctx, (const uint8_t * const*)frame->data, 
                          frame->linesize, 0, codec_ctx->height,
                          rgb_frame->data, rgb_frame->linesize);

                pthread_mutex_lock(&buffer_mutex);

                // Wait if the buffer is full
                while (buffer[write_index].is_full) {
                    pthread_cond_wait(&buffer_empty_cond, &buffer_mutex);
                }

                // Copy rgb_frame data to buffer
                if (buffer[write_index].frame == NULL) {
                    buffer[write_index].frame = av_frame_alloc();
                    av_image_fill_arrays(buffer[write_index].frame->data, buffer[write_index].frame->linesize, 
                                        (uint8_t *)av_malloc(numBytes * sizeof(uint8_t)),
                                        AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
                }
                
                av_image_copy(buffer[write_index].frame->data, buffer[write_index].frame->linesize,
                              (const uint8_t **)rgb_frame->data, rgb_frame->linesize,
                              AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height);
                
                buffer[write_index].is_full = 1;
                write_index = (write_index + 1) % BUFFER_SIZE;

                pthread_cond_signal(&buffer_full_cond);
                pthread_mutex_unlock(&buffer_mutex);
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    av_packet_free(&packet);
    
    return NULL;
}

gboolean display_frame(gpointer data) {
    pthread_mutex_lock(&buffer_mutex);

    // Check if buffer is empty
    if (!buffer[read_index].is_full) {
        pthread_mutex_unlock(&buffer_mutex);
        return TRUE;  // Keep the timer going
    }

    // Display the frame
    AVFrame *frame = buffer[read_index].frame;
    if (frame) {
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(
            frame->data[0], GDK_COLORSPACE_RGB, FALSE, 8,
            codec_ctx->width, codec_ctx->height, frame->linesize[0], NULL, NULL);

        // Create a GdkTexture from the pixbuf
        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);

        // Set the image
        gtk_image_set_from_paintable(GTK_IMAGE(image), GDK_PAINTABLE(texture));

        g_object_unref(texture);
        g_object_unref(pixbuf);
    }

    buffer[read_index].is_full = 0;
    read_index = (read_index + 1) % BUFFER_SIZE;

    pthread_cond_signal(&buffer_empty_cond);
    pthread_mutex_unlock(&buffer_mutex);

    return TRUE;
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    pthread_cancel(decoder_thread); // stop secondary thread first
    g_main_loop_quit(main_loop);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <video_file> <frame_rate>\n", argv[0]);
        return 1;
    }

    // Initialize GTK first (no parameters in GTK4)
    gtk_init();

    // Initialize FFmpeg
    avformat_network_init();

    if (avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0) {
        fprintf(stderr, "Could not open input file\n");
        return 1;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return 1;
    }

    // Find the first video stream
    int video_stream_index = -1;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "Could not find a video stream\n");
        return 1;
    }

    // Find decoder without casting
    codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Unsupported codec\n");
        return 1;
    }
    
    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return 1;
    }

    // Initialize buffer
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buffer[i].frame = NULL;
        buffer[i].is_full = 0;
    }

    pthread_mutex_init(&buffer_mutex, NULL);
    pthread_cond_init(&buffer_full_cond, NULL);
    pthread_cond_init(&buffer_empty_cond, NULL);

    // Set the frame rate
    frame_rate = atoi(argv[2]);

    // Create GTK window and image widget
    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Video Player");
    gtk_window_set_default_size(GTK_WINDOW(window), codec_ctx->width, codec_ctx->height);
    
    image = gtk_image_new();
    gtk_window_set_child(GTK_WINDOW(window), image);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    // Show the window
    gtk_window_present(GTK_WINDOW(window));

    // Create a decode thread
    pthread_create(&decoder_thread, NULL, decode_thread, NULL);

    // Set up GTK timer to display frames
    g_timeout_add(1000 / frame_rate, display_frame, NULL);

    // Start the main loop
    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    // Clean up
    pthread_join(decoder_thread, NULL);
    
    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (buffer[i].frame) {
            av_frame_free(&buffer[i].frame);
        }
    }
    
    if (rgb_buffer) {
        av_free(rgb_buffer);
    }
    
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    sws_freeContext(sws_ctx);

    pthread_mutex_destroy(&buffer_mutex);
    pthread_cond_destroy(&buffer_full_cond);
    pthread_cond_destroy(&buffer_empty_cond);
    
    g_main_loop_unref(main_loop);

    return 0;
}