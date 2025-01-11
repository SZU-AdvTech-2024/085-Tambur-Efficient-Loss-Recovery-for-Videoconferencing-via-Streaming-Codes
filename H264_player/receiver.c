#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>

#define LISTEN_PORT 5000
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

static int recv_all(int sockfd, uint8_t *buf, int size) {
    int received = 0;
    while (received < size) {
        int ret = recv(sockfd, buf + received, size - received, 0);
        if (ret <= 0) {
            return -1; // 出现错误或对端关闭
        }
        received += ret;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    //av_register_all();
    //avcodec_register_all();

    // 初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }

    SDL_Window *window = SDL_CreateWindow("Receiver", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                                          WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "SDL: could not create window - exiting\n");
        exit(1);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                                             WINDOW_WIDTH, WINDOW_HEIGHT);

    // 创建监听套接字
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(LISTEN_PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr)) < 0) {
        perror("bind error");
        exit(1);
    }

    if (listen(listenfd,1) < 0) {
        perror("listen error");
        exit(1);
    }

    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    int connfd = accept(listenfd,(struct sockaddr*)&client,&client_len);
    if (connfd < 0) {
        perror("accept error");
        exit(1);
    }

    // 准备H.264解码器
    const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder) {
        fprintf(stderr, "H.264 decoder not found.\n");
        return -1;
    }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        fprintf(stderr, "Failed to allocate decoder context.\n");
        return -1;
    }

    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        fprintf(stderr, "Failed to open decoder.\n");
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    struct SwsContext *sws_ctx = NULL;
    int got_first_params = 0;

    uint8_t size_buf[4]; // 用于存放接收到的帧大小字段
    uint8_t *frame_buf = NULL; // 用于存放接收到的一帧H.264数据
    int frame_count = 0;

    for (;;) {
        // 首先读取4字节的帧大小
        if (recv_all(connfd, size_buf, 4) < 0) {
            // 对端关闭或出错
            break;
        }

        // 转换字节序
        int32_t net_size = 0;
        memcpy(&net_size, size_buf, 4);
        int32_t frame_size = ntohl(net_size);

        if (frame_size <= 0) {
            // 无效的帧大小，可能对端结束发送
            break;
        }

        // 分配接收帧数据的缓冲区
        if (frame_buf) {
            free(frame_buf);
            frame_buf = NULL;
        }
        frame_buf = (uint8_t*)malloc(frame_size);
        if (!frame_buf) {
            fprintf(stderr, "Memory allocation failed.\n");
            break;
        }

        // 接收完整一帧数据
        if (recv_all(connfd, frame_buf, frame_size) < 0) {
            // 对端关闭或出错
            free(frame_buf);
            frame_buf = NULL;
            break;
        }

        // 将数据送入解码器
        av_packet_unref(pkt);
        pkt->data = frame_buf;
        pkt->size = frame_size;

        int ret = avcodec_send_packet(dec_ctx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            fprintf(stderr, "Error sending packet to decoder.\n");
            continue;
        }

        // 尝试从解码器获得解码后的帧
        while (1) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                // 尚无可用帧或解码结束
                break;
            } else if (ret < 0) {
                fprintf(stderr, "Error during decoding.\n");
                break;
            }

            // 一旦解码出一帧
            if (!got_first_params) {
                // 初始化SWS转换上下文（如果需要）
                // 在这里假设视频最终要渲染到WINDOW_WIDTH x WINDOW_HEIGHT
                sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                                         WINDOW_WIDTH, WINDOW_HEIGHT, AV_PIX_FMT_YUV420P,
                                         SWS_BICUBIC, NULL, NULL, NULL);
                got_first_params = 1;
            }

            // 转换到最终显示尺寸和像素格式
            AVFrame *display_frame = av_frame_alloc();
            display_frame->format = AV_PIX_FMT_YUV420P;
            display_frame->width = WINDOW_WIDTH;
            display_frame->height = WINDOW_HEIGHT;
            av_frame_get_buffer(display_frame, 32);

            sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                      0, dec_ctx->height, display_frame->data, display_frame->linesize);

            // 使用SDL显示
            SDL_UpdateYUVTexture(texture, NULL, 
                                 display_frame->data[0], display_frame->linesize[0],
                                 display_frame->data[1], display_frame->linesize[1],
                                 display_frame->data[2], display_frame->linesize[2]);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            av_frame_free(&display_frame);
            frame_count++;

            // 处理SDL事件（如窗口关闭）
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    goto end;
                }
            }
        }
    }

end:
    if (frame_buf) {
        free(frame_buf);
        frame_buf = NULL;
    }
    close(connfd);
    close(listenfd);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (sws_ctx) sws_freeContext(sws_ctx);
    avcodec_free_context(&dec_ctx);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
