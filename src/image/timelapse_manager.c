#include "timelapse_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TIMELAPSE_FRAME_SIZE (1024*1024)

// Use unified protocol definitions
typedef PackageHeader_t PKG_HEADER_S;
typedef PackageTail_t PKG_TAIL_S;

// Handle timelapse package - similar to handle_video_package
int handle_timelapse_package(const unsigned char* package, int pkg_len) {
    if (!package || pkg_len < 4) {
        printf("[Timelapse] Invalid package\n");
        return -1;
    }

    // Frame buffer for reassembly (static to persist across calls)
    typedef struct {
        unsigned char data[MAX_TIMELAPSE_FRAME_SIZE];
        int pkg_id;
        int task_id;
        int total_len;
        int used_len;
        int8_t file_type;
        int8_t frame_type;
        int valid;
    } FrameBuffer;
    
    static FrameBuffer frame_buf = {0};

    // Package structure:
    // offset 0-3: prefix ("@lif" for timelapse)
    // offset 4+: PKG_HEADER_S
    // offset 4+28: data or TAG_PKG_FILE_HEADER_S (if u8PkgSubHead==1)
    
    int offset = 4;  // Skip prefix "@lif"
    
    // Validate header size
    if (pkg_len < offset + sizeof(PKG_HEADER_S)) {
        printf("[Timelapse] Package header incomplete\n");
        return -1;
    }

    PKG_HEADER_S* header = (PKG_HEADER_S*)(package + offset);
    offset += sizeof(PKG_HEADER_S);

    if (header->u8PkgSubHead == 1) {
        // New frame start with file header
        if (pkg_len < offset + sizeof(TAG_PKG_FILE_HEADER_S)) {
            printf("[Timelapse] Package incomplete for file header\n");
            return -1;
        }

        TAG_PKG_FILE_HEADER_S* file_header = (TAG_PKG_FILE_HEADER_S*)(package + offset);
        offset += sizeof(TAG_PKG_FILE_HEADER_S);

        // Log frame info
        printf("[Timelapse] New frame: TaskId=%d, FileType=%d, FrameType=%d, FileLen=%d\n",
            file_header->s32TaskId, file_header->s8FileType, 
            file_header->s8FrameType, file_header->s32FileLength);

        // Initialize frame reassembly buffer
        frame_buf.pkg_id = header->u16PkgId;
        frame_buf.task_id = file_header->s32TaskId;
        frame_buf.file_type = file_header->s8FileType;
        frame_buf.frame_type = file_header->s8FrameType;
        frame_buf.total_len = file_header->s32FileLength;
        frame_buf.used_len = 0;
        frame_buf.valid = 1;

        // Add this frame data to buffer
        int data_len = pkg_len - offset - sizeof(PKG_TAIL_S);
        if (data_len > 0 && data_len <= MAX_TIMELAPSE_FRAME_SIZE) {
            memcpy(frame_buf.data, package + offset, data_len);
            frame_buf.used_len = data_len;
        }

        // Check if this is the last fragment (u16PkgIndex == 0 means last)
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                // Save frame to file
                char filename[256];
                const char* ext = (frame_buf.file_type == 1) ? "h265" : "h264";
                snprintf(filename, sizeof(filename), "timelapse_%d.%s", 
                         frame_buf.task_id, ext);
                
                FILE* out = fopen(filename, "ab");
                if (out) {
                    fwrite(frame_buf.data, 1, frame_buf.used_len, out);
                    fclose(out);
                    printf("[Timelapse] Saved frame: %d bytes to %s\n", 
                           frame_buf.used_len, filename);
                } else {
                    printf("[Timelapse] ERROR: Failed to open file: %s\n", filename);
                }
            }
            frame_buf.valid = 0;
        }

        return frame_buf.used_len;
    } else {
        // Fragment packet - must match existing frame buffer
        if (!frame_buf.valid || header->u16PkgId != frame_buf.pkg_id) {
            printf("[Timelapse] No matching frame buffer for fragment (PkgId=%d)\n",
                header->u16PkgId);
            return -1;
        }

        // Add fragment data to buffer
        int data_len = pkg_len - offset - sizeof(PKG_TAIL_S);
        if (data_len > 0 && frame_buf.used_len + data_len <= MAX_TIMELAPSE_FRAME_SIZE) {
            memcpy(frame_buf.data + frame_buf.used_len, package + offset, data_len);
            frame_buf.used_len += data_len;
        }

        // Check if this is the last fragment
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                // Save complete frame to file
                char filename[256];
                const char* ext = (frame_buf.file_type == 1) ? "h265" : "h264";
                snprintf(filename, sizeof(filename), "timelapse_%d.%s", 
                         frame_buf.task_id, ext);
                
                FILE* out = fopen(filename, "ab");
                if (out) {
                    fwrite(frame_buf.data, 1, frame_buf.used_len, out);
                    fclose(out);
                    printf("[Timelapse] Saved complete frame: %d bytes to %s\n", 
                           frame_buf.used_len, filename);
                } else {
                    printf("[Timelapse] ERROR: Failed to open file: %s\n", filename);
                }
            }
            frame_buf.valid = 0;
        }

        return data_len;
    }
}
