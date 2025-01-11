#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5000
#define OUTPUT_WIDTH 640
#define OUTPUT_HEIGHT 480

static int send_all(int sockfd, const uint8_t *buf, int size) {
    int sent = 0;
    while (sent < size) {
        int ret = send(sockfd, buf + sent, size - sent, 0);
        if (ret <= 0) {
            return -1; // error
        }
        sent += ret;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input_video_file\n", argv[0]);
        exit(1);
    }

    const char *input_file = argv[1];

    //av_register_all();

    AVFormatContext *ifmt_ctx = NULL;
    if (avformat_open_input(&ifmt_ctx, input_file, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open input file.\n");
        return -1;
    }

    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information.\n");
        return -1;
    }

    int video_stream_index = -1;
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index < 0) {
        fprintf(stderr, "No video stream found.\n");
        return -1;
    }

    AVCodecParameters *in_codecpar = ifmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec *decoder = avcodec_find_decoder(in_codecpar->codec_id);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        fprintf(stderr, "Failed to allocate decoder context.\n");
        return -1;
    }
    avcodec_parameters_to_context(dec_ctx, in_codecpar);
    if (avcodec_open2(dec_ctx, decoder, NULL) < 0) {
        fprintf(stderr, "Failed to open decoder.\n");
        return -1;
    }

    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        fprintf(stderr, "H.264 encoder not found.\n");
        return -1;
    }

    AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        fprintf(stderr, "Failed to allocate encoder context.\n");
        return -1;
    }

    enc_ctx->width = OUTPUT_WIDTH;
    enc_ctx->height = OUTPUT_HEIGHT;
    enc_ctx->time_base = (AVRational){1, 25};
    enc_ctx->framerate = (AVRational){25, 1};
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->gop_size = 12;
    enc_ctx->max_b_frames = 2;
    enc_ctx->bit_rate = 400000;

    if (avcodec_open2(enc_ctx, encoder, NULL) < 0) {
        fprintf(stderr, "Failed to open encoder.\n");
        return -1;
    }

    struct SwsContext *sws_ctx = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                                                enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
                                                SWS_BICUBIC, NULL, NULL, NULL);

    AVFrame *frame = av_frame_alloc();
    AVFrame *enc_frame = av_frame_alloc();
    enc_frame->format = enc_ctx->pix_fmt;
    enc_frame->width = enc_ctx->width;
    enc_frame->height = enc_ctx->height;
    av_frame_get_buffer(enc_frame, 32);

    AVPacket *pkt = av_packet_alloc();

    // 创建TCP套接字并连接接收端
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect failed");
        return -1;
    }

    // 开始循环读取、解码、编码并发送
    while (1) {
        AVPacket input_pkt = {0};
        //av_init_packet(&input_pkt);
        input_pkt.data = NULL;
        input_pkt.size = 0;
        if (av_read_frame(ifmt_ctx, &input_pkt) < 0) {
            // 已到达文件末尾或出错，发送空帧给编码器以flush
            break;
        }

        if (input_pkt.stream_index == video_stream_index) {
            int ret = avcodec_send_packet(dec_ctx, &input_pkt);
            av_packet_unref(&input_pkt);
            if (ret < 0) {
                fprintf(stderr, "Error sending a packet for decoding.\n");
                continue;
            }

            // 从解码器拿出帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    fprintf(stderr, "Error during decoding.\n");
                    break;
                }

                // 转换像素格式和大小
                sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0,
                          dec_ctx->height, enc_frame->data, enc_frame->linesize);
                enc_frame->pts = frame->pts;

                // 将转换后的帧送入编码器
                ret = avcodec_send_frame(enc_ctx, enc_frame);
                if (ret < 0) {
                    fprintf(stderr, "Error sending frame to encoder.\n");
                    continue;
                }

                // 从编码器获取编码后的数据（H.264裸流）
                while (1) {
                    ret = avcodec_receive_packet(enc_ctx, pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        fprintf(stderr, "Error during encoding.\n");
                        break;
                    }

                    // 此处pkt即为一个完整编码帧的数据
                    // 我们先发送数据大小
                    int32_t frame_size = pkt->size;
                    int32_t net_size = htonl(frame_size);

                    // 先发送长度
                    if (send_all(sockfd, (uint8_t*)&net_size, sizeof(net_size)) < 0) {
                        perror("send size error");
                        av_packet_unref(pkt);
                        break;
                    }

                    // 再发送数据本身
                    if (send_all(sockfd, pkt->data, pkt->size) < 0) {
                        perror("send data error");
                        av_packet_unref(pkt);
                        break;
                    }

                    av_packet_unref(pkt);
                }
            }
        } else {
            // 非视频流直接丢弃
            av_packet_unref(&input_pkt);
        }
    }

    // 冲刷编码器的剩余数据
    avcodec_send_frame(enc_ctx, NULL);
    while (1) {
        int ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EOF) || ret == AVERROR(EAGAIN))
            break;

        int32_t frame_size = pkt->size;
        int32_t net_size = htonl(frame_size);
        if (send_all(sockfd, (uint8_t*)&net_size, sizeof(net_size)) < 0) {
            perror("send size error");
            break;
        }
        if (send_all(sockfd, pkt->data, pkt->size) < 0) {
            perror("send data error");
            break;
        }
        av_packet_unref(pkt);
    }

    close(sockfd);

    av_frame_free(&frame);
    av_frame_free(&enc_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_close_input(&ifmt_ctx);
    sws_freeContext(sws_ctx);

    return 0;
}
