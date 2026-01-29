#include "video_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg 头文件（需要从官网下载的开发包）
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

// 解码器结构
struct VideoDecoder {
    AVCodec* codec;
    AVCodecContext* codec_ctx;
    AVCodecParserContext* parser;  // 解析器
    AVFrame* frame;                // 解码后的原始帧
    AVFrame* scaled_frame;         // 缩放后的帧
    AVPacket* packet;
    struct SwsContext* sws_ctx;    // 缩放上下文
    
    FrameCallback callback;
    void* user_data;
    
    int codec_type;
    int initialized;
    
    // 缩放配置
    int scale_enabled;             // 是否启用缩放
    int target_width;              // 目标宽度
    int target_height;             // 目标高度
};

/**
 * 创建视频解码器
 */
VideoDecoder* video_decoder_create(int codec_type, 
                                   FrameCallback frame_callback, 
                                   void* user_data) {
    VideoDecoder* decoder = (VideoDecoder*)malloc(sizeof(VideoDecoder));
    if (!decoder) {
        printf("[Decoder] Failed to allocate decoder\n");
        return NULL;
    }
    
    memset(decoder, 0, sizeof(VideoDecoder));
    decoder->codec_type = codec_type;
    decoder->callback = frame_callback;
    decoder->user_data = user_data;
    
    // 根据编码类型选择解码器
    enum AVCodecID codec_id;
    const char* codec_name;
    
    switch (codec_type) {
        case 1: // H.264
            codec_id = AV_CODEC_ID_H264;
            codec_name = "H.264";
            break;
        case 2: // H.265
            codec_id = AV_CODEC_ID_HEVC;
            codec_name = "H.265";
            break;
        case 3: // JPEG
            codec_id = AV_CODEC_ID_MJPEG;
            codec_name = "MJPEG";
            break;
        default:
            printf("[Decoder] Unknown codec type: %d\n", codec_type);
            free(decoder);
            return NULL;
    }
    
    printf("[Decoder] Creating %s decoder...\n", codec_name);
    
    // 查找解码器
    decoder->codec = avcodec_find_decoder(codec_id);
    if (!decoder->codec) {
        printf("[Decoder] Codec not found: %s\n", codec_name);
        free(decoder);
        return NULL;
    }
    
    // 创建解码器上下文
    decoder->codec_ctx = avcodec_alloc_context3(decoder->codec);
    if (!decoder->codec_ctx) {
        printf("[Decoder] Failed to allocate codec context\n");
        free(decoder);
        return NULL;
    }
    
    // 打开解码器
    if (avcodec_open2(decoder->codec_ctx, decoder->codec, NULL) < 0) {
        printf("[Decoder] Failed to open codec\n");
        avcodec_free_context(&decoder->codec_ctx);
        free(decoder);
        return NULL;
    }
    
    // 创建解析器
    decoder->parser = av_parser_init(codec_id);
    if (!decoder->parser) {
        printf("[Decoder] Failed to create parser\n");
        avcodec_free_context(&decoder->codec_ctx);
        free(decoder);
        return NULL;
    }
    printf("[Decoder] Parser created successfully\n");
    
    // 分配帧和数据包
    decoder->frame = av_frame_alloc();
    decoder->packet = av_packet_alloc();
    
    if (!decoder->frame || !decoder->packet) {
        printf("[Decoder] Failed to allocate frame or packet\n");
        if (decoder->frame) av_frame_free(&decoder->frame);
        if (decoder->packet) av_packet_free(&decoder->packet);
        avcodec_free_context(&decoder->codec_ctx);
        free(decoder);
        return NULL;
    }
    
    decoder->initialized = 1;
    printf("[Decoder] %s decoder created successfully\n", codec_name);
    
    return decoder;
}

/**
 * 输入编码数据到解码器
 */
int video_decoder_decode(VideoDecoder* decoder, 
                        const unsigned char* data, 
                        int len, 
                        int64_t pts) {
    if (!decoder || !decoder->initialized) {
        return -1;
    }
    
    // 使用解析器解析数据流
    const uint8_t* parse_data = (const uint8_t*)data;
    int parse_size = len;
    int total_decoded = 0;
    
    while (parse_size > 0) {
        // 解析出一个完整的帧
        int ret = av_parser_parse2(
            decoder->parser,
            decoder->codec_ctx,
            &decoder->packet->data,
            &decoder->packet->size,
            parse_data,
            parse_size,
            AV_NOPTS_VALUE,
            AV_NOPTS_VALUE,
            0
        );
        
        if (ret < 0) {
            printf("[Decoder] Error parsing frame: %d\n", ret);
            return ret;
        }
        
        parse_data += ret;
        parse_size -= ret;
        
        // 如果解析出了完整的包，进行解码
        if (decoder->packet->size > 0) {
            decoder->packet->pts = pts;
            
            // 发送数据包到解码器
            ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
            if (ret < 0) {
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                    printf("[Decoder] Error sending packet: %d\n", ret);
                }
                continue;
            }
            
            // 尝试接收所有可用的解码帧
            while (1) {
                ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
                
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // 需要更多数据或已结束
                    break;
                } else if (ret < 0) {
                    printf("[Decoder] Error receiving frame: %d\n", ret);
                    break;
                }
                
                // 成功解码一帧
                total_decoded++;
                
                // 如果设置了回调函数，立即调用
                if (decoder->callback) {
                    VideoFrame vframe;
                    AVFrame* output_frame = decoder->frame;
                    
                    // 如果启用了缩放，执行缩放操作
                    if (decoder->scale_enabled && decoder->scaled_frame) {
                        // 确保 sws_ctx 已初始化
                        if (!decoder->sws_ctx) {
                            decoder->sws_ctx = sws_getContext(
                                decoder->frame->width, decoder->frame->height, decoder->frame->format,
                                decoder->target_width, decoder->target_height, AV_PIX_FMT_YUV420P,
                                SWS_BILINEAR, NULL, NULL, NULL
                            );
                            
                            if (!decoder->sws_ctx) {
                                printf("[Decoder] Failed to create scaling context\n");
                            } else {
                                printf("[Decoder] Created scaling context: %dx%d -> %dx%d\n",
                                       decoder->frame->width, decoder->frame->height,
                                       decoder->target_width, decoder->target_height);
                            }
                        }
                        
                        if (decoder->sws_ctx) {
                            // 执行缩放
                            sws_scale(decoder->sws_ctx,
                                     (const uint8_t* const*)decoder->frame->data,
                                     decoder->frame->linesize,
                                     0, decoder->frame->height,
                                     decoder->scaled_frame->data,
                                     decoder->scaled_frame->linesize);
                            
                            output_frame = decoder->scaled_frame;
                        }
                    }
                    
                    // 填充回调帧数据
                    vframe.width = output_frame->width;
                    vframe.height = output_frame->height;
                    vframe.pts = decoder->frame->pts;
                    
                    vframe.data[0] = output_frame->data[0];
                    vframe.data[1] = output_frame->data[1];
                    vframe.data[2] = output_frame->data[2];
                    
                    vframe.linesize[0] = output_frame->linesize[0];
                    vframe.linesize[1] = output_frame->linesize[1];
                    vframe.linesize[2] = output_frame->linesize[2];
                    
                    decoder->callback(&vframe, decoder->user_data);
                }
            }
        }
    }
    
    return total_decoded;
}

/**
 * 获取解码后的帧
 */
int video_decoder_get_frame(VideoDecoder* decoder, VideoFrame* frame) {
    if (!decoder || !decoder->initialized || !frame) {
        return -1;
    }
    
    int ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;  // 没有帧
    } else if (ret < 0) {
        return ret;  // 错误
    }
    
    // 填充帧数据
    frame->width = decoder->frame->width;
    frame->height = decoder->frame->height;
    frame->pts = decoder->frame->pts;
    
    frame->data[0] = decoder->frame->data[0];
    frame->data[1] = decoder->frame->data[1];
    frame->data[2] = decoder->frame->data[2];
    
    frame->linesize[0] = decoder->frame->linesize[0];
    frame->linesize[1] = decoder->frame->linesize[1];
    frame->linesize[2] = decoder->frame->linesize[2];
    
    return 1;  // 有帧
}

/**
 * 获取解码器信息
 */
int video_decoder_get_info(VideoDecoder* decoder, int* width, int* height) {
    if (!decoder || !decoder->initialized) {
        return -1;
    }
    
    if (width) *width = decoder->codec_ctx->width;
    if (height) *height = decoder->codec_ctx->height;
    
    return 0;
}

/**
 * 设置输出缩放
 */
int video_decoder_set_scale(VideoDecoder* decoder, int target_width, int target_height) {
    if (!decoder || !decoder->initialized) {
        return -1;
    }
    
    // 如果目标尺寸为 0，禁用缩放
    if (target_width <= 0 || target_height <= 0) {
        printf("[Decoder] Disabling scaling\n");
        decoder->scale_enabled = 0;
        
        if (decoder->scaled_frame) {
            av_frame_free(&decoder->scaled_frame);
            decoder->scaled_frame = NULL;
        }
        
        if (decoder->sws_ctx) {
            sws_freeContext(decoder->sws_ctx);
            decoder->sws_ctx = NULL;
        }
        
        return 0;
    }
    
    printf("[Decoder] Setting scale to %dx%d\n", target_width, target_height);
    
    decoder->target_width = target_width;
    decoder->target_height = target_height;
    decoder->scale_enabled = 1;
    
    // 分配缩放后的帧
    if (!decoder->scaled_frame) {
        decoder->scaled_frame = av_frame_alloc();
        if (!decoder->scaled_frame) {
            printf("[Decoder] Failed to allocate scaled frame\n");
            decoder->scale_enabled = 0;
            return -1;
        }
    }
    
    // 设置缩放帧的参数
    decoder->scaled_frame->format = AV_PIX_FMT_YUV420P;
    decoder->scaled_frame->width = target_width;
    decoder->scaled_frame->height = target_height;
    
    // 分配缓冲区
    int ret = av_frame_get_buffer(decoder->scaled_frame, 32);
    if (ret < 0) {
        printf("[Decoder] Failed to allocate scaled frame buffer: %d\n", ret);
        av_frame_free(&decoder->scaled_frame);
        decoder->scale_enabled = 0;
        return -1;
    }
    
    printf("[Decoder] Scale configured successfully\n");
    return 0;
}

/**
 * 销毁解码器
 */
void video_decoder_destroy(VideoDecoder* decoder) {
    if (!decoder) return;
    
    printf("[Decoder] Destroying decoder...\n");
    
    if (decoder->parser) {
        av_parser_close(decoder->parser);
    }
    
    if (decoder->sws_ctx) {
        sws_freeContext(decoder->sws_ctx);
    }
    
    if (decoder->scaled_frame) {
        av_frame_free(&decoder->scaled_frame);
    }
    
    if (decoder->frame) {
        av_frame_free(&decoder->frame);
    }
    
    if (decoder->packet) {
        av_packet_free(&decoder->packet);
    }
    
    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
    }
    
    free(decoder);
    printf("[Decoder] Decoder destroyed\n");
}