// Video Decoder Header
#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H
#include <stdint.h>

typedef struct VideoDecoder VideoDecoder;

typedef struct VideoFrame {
    uint8_t* data[3];      // YUV 데이터 포인터 (Y, U, V)
    int linesize[3];       // 각 평면의 행 크기
    int width;             // 프레임 너비
    int height;            // 프레임 높이
    int64_t pts;           // 프레젠테이션 타임스탬프
} VideoFrame;

// 콜백 함수 타입
typedef void (*FrameCallback)(VideoFrame* frame, void* user_data);

VideoDecoder* video_decoder_create(int codec_type, FrameCallback frame_callback, void* user_data);
int video_decoder_decode(VideoDecoder* decoder, const unsigned char* data, int len, int64_t pts);
void video_decoder_destroy(VideoDecoder* decoder);
int video_decoder_set_scale(VideoDecoder* decoder, int w, int h);

#endif // VIDEO_DECODER_H
