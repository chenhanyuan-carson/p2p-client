#include "video_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "video_decoder.h"
#include "video_display.h"
#include "protocol_defs.h"

#define MAX_VIDEO_FRAME_SIZE (1024*1024)

// Use unified protocol definitions
typedef PackageHeader_t PKG_HEADER_S;
typedef PackageTail_t PKG_TAIL_S;

VideoStreamManager* create_video_stream_manager(const char* output_file_prefix) {
    VideoStreamManager* mgr = (VideoStreamManager*)malloc(sizeof(VideoStreamManager));
    if (!mgr) return NULL;
    memset(mgr, 0, sizeof(VideoStreamManager));
    mgr->active_stream_count = 0;
    printf("[VideoMgr] Stream manager created\n");
    return mgr;
}

VideoStream* create_video_stream(int stream_type, const char* output_file_prefix, int codec_type) {
    VideoStream* stream = (VideoStream*)malloc(sizeof(VideoStream));
    if (!stream) return NULL;
    memset(stream, 0, sizeof(VideoStream));
    stream->stream_type = stream_type;
    stream->codec_type = codec_type;
    stream->running = 1; // Initialize stream as active

    const char* extension = (codec_type == 3) ? "jpg" : "h265";
    if (codec_type == 3) {
        stream->output_file = NULL;
        printf("[Stream%d] JPEG stream initialized (frames saved individually)\n", stream_type);
    } else {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s_stream%d.%s", output_file_prefix, stream_type, extension);
        stream->output_file = fopen(filename, "wb");
        if (!stream->output_file) {
            printf("[Stream%d] Warning: Failed to open output file: %s\n", stream_type, filename);
        } else {
            printf("[Stream%d] Output file created: %s\n", stream_type, filename);
        }
    }
    return stream;
}

void save_video_frame(VideoStream* stream, const unsigned char* frame_data, int frame_size) {
    if (!stream) return;
    if (stream->codec_type == 3) {
        char filename[256];
        snprintf(filename, sizeof(filename), "output_video_stream%d_frame%06d.jpg", stream->stream_type, stream->frame_count);
        FILE* jpg_file = fopen(filename, "wb");
        if (!jpg_file) {
            printf("[Stream%d] ERROR: Failed to create JPEG file: %s\n", stream->stream_type, filename);
            return;
        }
        size_t written = fwrite(frame_data, 1, frame_size, jpg_file);
        fclose(jpg_file);
        if (written != frame_size) {
            printf("[Stream%d] ERROR: Failed to write JPEG frame to %s\n", stream->stream_type, filename);
            return;
        }
        printf("[Stream%d] JPEG saved: %s (%d bytes)\n", stream->stream_type, filename, frame_size);
    } else {
        if (!stream->output_file) return;
        size_t written = fwrite(frame_data, 1, frame_size, stream->output_file);
        if (written != frame_size) {
            printf("[Stream%d] ERROR: Failed to write video frame\n", stream->stream_type);
            return;
        }
    }
    stream->total_bytes += frame_size;
}

void destroy_video_stream(VideoStream* stream) {
    if (!stream) return;
    if (stream->display) video_display_destroy(stream->display);
    if (stream->decoder) video_decoder_destroy(stream->decoder);
    if (stream->output_file) fclose(stream->output_file);
    printf("[Stream%d] Statistics: %d frames, %.2f MB\n", stream->stream_type, stream->frame_count, (float)stream->total_bytes / (1024*1024));
    free(stream);
}

VideoStream* get_or_create_stream(VideoStreamManager* mgr, int stream_type, const char* output_file_prefix, int codec_type) {
    if (!mgr || stream_type < 1 || stream_type > 5) return NULL;
    // Prevent recreation of stream after stop
    if (mgr->streams[stream_type - 1] && !mgr->streams[stream_type - 1]->running) {
        printf("[Stream%d] Stream already stopped, skipping recreation\n", stream_type);
        return NULL;
    }
    if (mgr->streams[stream_type - 1] != NULL) return mgr->streams[stream_type - 1];
    VideoStream* stream = create_video_stream(stream_type, output_file_prefix, codec_type);
    if (stream) {
        mgr->streams[stream_type - 1] = stream;
        mgr->active_stream_count++;
        printf("[VideoMgr] Created stream type %d, total active: %d\n", stream_type, mgr->active_stream_count);
    }
    return stream;
}

void destroy_video_stream_manager(VideoStreamManager* mgr) {
    if (!mgr) return;
    for (int i = 0; i < 5; i++) {
        if (mgr->streams[i]) {
            destroy_video_stream(mgr->streams[i]);
            mgr->streams[i] = NULL;
        }
    }
    free(mgr);
    printf("[VideoMgr] Stream manager destroyed\n");
}

// Video frame decode callback - to be called by video_decoder
void on_frame_decoded(VideoFrame* frame, void* user_data) {
    VideoFrame* vf = frame;
    VideoStream* stream = (VideoStream*)user_data;
    if (!stream || !stream->display) return;
    if (stream->frame_count % 30 == 0) {
        printf("[Stream%d] Decoded frame: %dx%d, PTS: %lld\n", stream->stream_type, vf->width, vf->height, vf->pts);
    }
    int ret = video_display_render(stream->display, vf);
    if (ret < 0 && stream->frame_count % 30 == 0) {
        printf("[Stream%d] Failed to render frame: %d\n", stream->stream_type, ret);
    }
    if (!video_display_poll_events(stream->display)) {
        printf("[Stream%d] Display window closed by user\n", stream->stream_type);
    }
}

// Poll display events for all streams
int video_manager_poll_events(VideoStreamManager* mgr) {
    if (!mgr) return 0;
    for (int i = 0; i < 5; i++) {
        if (mgr->streams[i] && mgr->streams[i]->display) {
            if (!video_display_poll_events(mgr->streams[i]->display)) return 0;
        }
    }
    return 1;
}

// Stop a specific stream: destroy its decoder and close its display (keeps stream object)
void video_manager_stop_stream(VideoStreamManager* mgr, int stream_type) {
    if (!mgr) return;
    if (stream_type < 1 || stream_type > 5) return;
    VideoStream* s = mgr->streams[stream_type - 1];
    if (!s) return;
    if (s->decoder) {
        video_decoder_destroy(s->decoder);
        s->decoder = NULL;
        printf("[Stream%d] Decoder destroyed on stop\n", stream_type);
    }
    if (s->display) {
        video_display_destroy(s->display);
        s->display = NULL;
        printf("[Stream%d] Display closed on stop\n", stream_type);
    }
    s->running = 0; // Mark stream as stopped
}

const char* get_stream_type_name(int stream_type) {
    switch(stream_type) {
        case 1: return "Main Stream";
        case 2: return "Sub Stream";
        case 3: return "Playback";
        case 4: return "Talk";
        case 5: return "Download";
        default: return "Unknown";
    }
}

// Handle video package - parse headers, reassemble, decode and display
int handle_video_package(VideoStreamManager* mgr, const unsigned char* package, int pkg_len) {
    if (!mgr || !package || pkg_len < 4) {
        printf("[Video] Invalid video package\n");
        return -1;
    }

    // Frame buffer for reassembly (static to persist across multiple calls for same frame)
    typedef struct {
        unsigned char data[MAX_VIDEO_FRAME_SIZE];
        int pkg_id;
        int stream_type;
        int total_len;
        int used_len;
        VideoStream* stream;
        TAG_PKG_VIDEO_HEADER_S video_header;
        int valid;
    } FrameBuffer;
    static FrameBuffer frame_buf = {0};

    // Package structure:
    // offset 0-3: prefix ("$div" for video)
    // offset 4+: TAG_PKG_HEADER_S
    // offset 4+28: video data or TAG_PKG_VIDEO_HEADER_S (if u8PkgSubHead==1)
    
    int offset = 4;  // Skip prefix
    
    // Validate header size
    if (pkg_len < offset + sizeof(PKG_HEADER_S)) {
        printf("[Video] Package header incomplete (pkg_len=%d, need=%d)\n", pkg_len, (int)(offset + sizeof(PKG_HEADER_S)));
        return -1;
    }

    PKG_HEADER_S* header = (PKG_HEADER_S*)(package + offset);
    offset += sizeof(PKG_HEADER_S);

    VideoStream* stream = NULL;
    int stream_type = -1;

    if (header->u8PkgSubHead == 1) {
        // New frame start with video header
        if (pkg_len < offset + sizeof(TAG_PKG_VIDEO_HEADER_S)) {
            printf("[Video] Package incomplete for video header\n");
            return -1;
        }

        TAG_PKG_VIDEO_HEADER_S* video_header = (TAG_PKG_VIDEO_HEADER_S*)(package + offset);
        offset += sizeof(TAG_PKG_VIDEO_HEADER_S);
        stream_type = video_header->s8StreamType;

        // Log frame info (every 10th frame to reduce spam)
        static int log_counter = 0;
        if (log_counter++ % 10 == 0) {
            printf("[Video] Stream:%d(%s), Encode:%d, Frame:%d, Res:%dx%d, FPS:%d, Length:%d\n",
                stream_type, get_stream_type_name(stream_type),
                video_header->s8EncodeType, video_header->s8FrameType,
                video_header->u16VideoWidth, video_header->u16VideoHeight,
                video_header->u8FrameRate, video_header->s32FrameLen);
        }

        // Get or create stream
        stream = get_or_create_stream(mgr, stream_type, "output_video", video_header->s8EncodeType);
        if (!stream) {
            printf("[Video] Failed to get stream for type %d\n", stream_type);
            return -1;
        }

        memcpy(&stream->last_header, video_header, sizeof(TAG_PKG_VIDEO_HEADER_S));

        // Initialize decoder/display on first frame of stream
        if (!stream->decoder && video_header->s8EncodeType > 0) {
            stream->codec_type = video_header->s8EncodeType;
            stream->video_width = video_header->u16VideoWidth;
            stream->video_height = video_header->u16VideoHeight;

            // JPEG: save directly without decoder
            if (video_header->s8EncodeType == 3) {
                printf("[Stream%d] JPEG detected, will save directly without decoding\n", stream_type);
            } else {
                // H.264/H.265: create decoder and display
                int display_width, display_height;
                if (stream->video_width > 1280) {
                    display_width = 1280;
                    display_height = 720;
                    printf("[Stream%d] 1080p detected, scaling to 720p\n", stream_type);
                } else {
                    display_width = stream->video_width;
                    display_height = stream->video_height;
                    printf("[Stream%d] Resolution acceptable, no scaling\n", stream_type);
                }

                char window_title[128];
                snprintf(window_title, sizeof(window_title), "P2P %s - %dx%d",
                    get_stream_type_name(stream_type), display_width, display_height);

                printf("[Stream%d] Creating display window (%dx%d)...\n", stream_type, display_width, display_height);
                stream->display = video_display_create(window_title, display_width, display_height);
                if (!stream->display) {
                    printf("[Stream%d] Warning: Failed to create display window\n", stream_type);
                }

                printf("[Stream%d] Creating decoder (type: %d)...\n", stream_type, stream->codec_type);
                stream->decoder = video_decoder_create(stream->codec_type, on_frame_decoded, stream);
                if (!stream->decoder) {
                    printf("[Stream%d] Warning: Failed to create decoder\n", stream_type);
                } else {
                    // Set scaling if needed
                    if (display_width != stream->video_width || display_height != stream->video_height) {
                        printf("[Stream%d] Setting decoder scale: %dx%d -> %dx%d\n", stream_type,
                            stream->video_width, stream->video_height, display_width, display_height);
                        int ret = video_decoder_set_scale(stream->decoder, display_width, display_height);
                        if (ret < 0) {
                            printf("[Stream%d] Warning: Failed to set scale\n", stream_type);
                        }
                    }
                }
            }
        }

        // Initialize frame reassembly buffer
        frame_buf.pkg_id = header->u16PkgId;
        frame_buf.stream_type = stream_type;
        frame_buf.total_len = 0;
        frame_buf.used_len = 0;
        frame_buf.stream = stream;
        memcpy(&frame_buf.video_header, video_header, sizeof(TAG_PKG_VIDEO_HEADER_S));
        frame_buf.valid = 1;

        // Add this frame data to buffer
        int video_data_len = pkg_len - offset - sizeof(PKG_TAIL_S);
        if (video_data_len > 0 && frame_buf.used_len + video_data_len <= MAX_VIDEO_FRAME_SIZE) {
            memcpy(frame_buf.data + frame_buf.used_len, package + offset, video_data_len);
            frame_buf.used_len += video_data_len;
        }

        // Check if this is the last fragment (u16PkgIndex == 0 means last)
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                // Save frame to file
                save_video_frame(frame_buf.stream, frame_buf.data, frame_buf.used_len);
                frame_buf.stream->frame_count++;
                frame_buf.stream->total_bytes += frame_buf.used_len;
                uint64_t pts = frame_buf.video_header.u64Pts;

                // Decode and display
                if (frame_buf.stream->codec_type == 3) {
                    // JPEG: already saved
                    printf("[Stream%d] JPEG frame saved directly (size: %d bytes)\n",
                        frame_buf.stream->stream_type, frame_buf.used_len);
                } else {
                    // H.264/H.265: decode
                    if (frame_buf.stream->decoder) {
                        int ret = video_decoder_decode(frame_buf.stream->decoder, frame_buf.data, frame_buf.used_len, pts);
                        if (ret < 0) {
                            printf("[Stream%d] Warning: Decode error %d\n", frame_buf.stream->stream_type, ret);
                        }
                    }
                }
            }
            frame_buf.valid = 0;
        }

        return video_data_len;
    } else {
        // Fragment packet - must match existing frame buffer
        if (!frame_buf.valid || header->u16PkgId != frame_buf.pkg_id) {
            static int skip_count = 0;
            if (skip_count++ % 30 == 0) {
                printf("[Video] No matching frame buffer for fragment (PkgId=%d, skipped %d)\n",
                    header->u16PkgId, skip_count);
            }
            return -1;
        }

        // Add fragment data to buffer
        int video_data_len = pkg_len - offset - sizeof(PKG_TAIL_S);
        if (video_data_len > 0 && frame_buf.used_len + video_data_len <= MAX_VIDEO_FRAME_SIZE) {
            memcpy(frame_buf.data + frame_buf.used_len, package + offset, video_data_len);
            frame_buf.used_len += video_data_len;
        }

        // Check if this is the last fragment
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                // Save frame to file
                save_video_frame(frame_buf.stream, frame_buf.data, frame_buf.used_len);
                frame_buf.stream->frame_count++;
                frame_buf.stream->total_bytes += frame_buf.used_len;
                uint64_t pts = frame_buf.video_header.u64Pts;

                // Decode and display
                if (frame_buf.stream->codec_type == 3) {
                    // JPEG: already saved
                    printf("[Stream%d] JPEG frame saved directly (size: %d bytes)\n",
                        frame_buf.stream->stream_type, frame_buf.used_len);
                } else {
                    // H.264/H.265: decode
                    if (frame_buf.stream->decoder) {
                        int ret = video_decoder_decode(frame_buf.stream->decoder, frame_buf.data, frame_buf.used_len, pts);
                        if (ret < 0) {
                            printf("[Stream%d] Warning: Decode error %d\n", frame_buf.stream->stream_type, ret);
                        }
                    }
                }
            }
            frame_buf.valid = 0;
        }

        return video_data_len;
    }
}
