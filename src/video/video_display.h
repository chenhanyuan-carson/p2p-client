// Video Display Header
#ifndef VIDEO_DISPLAY_H
#define VIDEO_DISPLAY_H
#include "video_decoder.h"
#include <stdint.h>

// 显示窗口句柄
typedef struct VideoDisplay VideoDisplay;

VideoDisplay* video_display_create(const char* title, int width, int height);
int video_display_render(VideoDisplay* display, VideoFrame* frame);
int video_display_poll_events(VideoDisplay* display);
void video_display_set_title(VideoDisplay* display, const char* title);
void video_display_destroy(VideoDisplay* display);

#endif // VIDEO_DISPLAY_H