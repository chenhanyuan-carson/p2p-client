#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include "video_decoder.h"
#include "video_display.h"
#include "protocol_defs.h"
#include <stdio.h>
#include <stdint.h>

typedef struct {
    int stream_type;
    FILE* output_file;
    int frame_count;
    unsigned long long total_bytes;
    VideoDecoder* decoder;
    VideoDisplay* display;
    int codec_type;
    int video_width;
    int video_height;
    TAG_PKG_VIDEO_HEADER_S last_header;    // Last video header for reassembly
    int running; // Indicates if the stream is active
} VideoStream;

typedef struct {
    VideoStream* streams[5];
    int active_stream_count;
} VideoStreamManager;

VideoStreamManager* create_video_stream_manager(const char* output_file_prefix);
void destroy_video_stream_manager(VideoStreamManager* mgr);
int handle_video_package(VideoStreamManager* mgr, const unsigned char* package, int pkg_len);
void on_frame_decoded(VideoFrame* frame, void* user_data);

// Stop a specific stream: destroy its decoder and close its display (keeps stream object)
void video_manager_stop_stream(VideoStreamManager* mgr, int stream_type);

// Poll display events for all managed streams; returns 0 if any display closed
int video_manager_poll_events(VideoStreamManager* mgr);

#endif // VIDEO_MANAGER_H
