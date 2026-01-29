#include "image_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "protocol_defs.h"

#define MAX_VIDEO_FRAME_SIZE (1024*1024)

// Use unified protocol definitions
typedef PackageHeader_t TAG_PKG_HEADER_S;
typedef ImageStreamHeader_t TAG_PKG_IMAGE_HEADER_S;

int handle_image_package(const unsigned char* package, int pkg_len) {
    if (pkg_len < 4) return -1;
    // Copy of original implementation simplified and moved here
    // Caller should pass full package starting with "$gmi"
    int offset = 4;

    const TAG_PKG_HEADER_S* header = (const TAG_PKG_HEADER_S*)(package + offset);
    offset += sizeof(TAG_PKG_HEADER_S);

    static struct ImageBuffer {
        unsigned short pkg_id;
        int total_len;
        int used_len;
        unsigned char data[MAX_VIDEO_FRAME_SIZE];
        char filename[256];
        int has_header;
        TAG_PKG_IMAGE_HEADER_S image_header;
    } image_buf = {0};

    if (header->u8PkgSubHead == 1) {
        if (header->u16PkgLen < sizeof(TAG_PKG_IMAGE_HEADER_S)) return -1;
        const TAG_PKG_IMAGE_HEADER_S* image_header = (const TAG_PKG_IMAGE_HEADER_S*)(package + offset);
        offset += sizeof(TAG_PKG_IMAGE_HEADER_S);
        image_buf.pkg_id = header->u16PkgId;
        image_buf.total_len = image_header->s32ImageLen;
        image_buf.used_len = 0;
        image_buf.has_header = 1;
        memcpy(&image_buf.image_header, image_header, sizeof(TAG_PKG_IMAGE_HEADER_S));
        const char* ext = (image_header->s8EncodeType == 1) ? "jpg" : "png";
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", tm_info);
        snprintf(image_buf.filename, sizeof(image_buf.filename), "snapshot_ch%d_%s_%llu.%s",
                 image_header->s8Ch, time_str, image_header->u64Pts, ext);
        printf("[Image] Start new image: %s (total %d bytes)\n", image_buf.filename, image_buf.total_len);
        return 0;
    }

    if (header->u8PkgSubHead == 0) {
        if (image_buf.pkg_id == 0 || image_buf.pkg_id != header->u16PkgId) {
            printf("[Image] No valid image header for data packet\n");
            return -1;
        }
        int data_len = header->u16PkgLen;
        if (data_len <= 0) return -1;
        if (image_buf.used_len + data_len > MAX_VIDEO_FRAME_SIZE) { memset(&image_buf,0,sizeof(image_buf)); return -1; }
        memcpy(image_buf.data + image_buf.used_len, package + offset, data_len);
        image_buf.used_len += data_len;
        //printf("[Image] Accumulated %d/%d bytes for PkgId %d\n", image_buf.used_len, image_buf.total_len, image_buf.pkg_id);
        if (header->u16PkgIndex == 0) {
            FILE* image_file = fopen(image_buf.filename, "wb");
            if (!image_file) { memset(&image_buf,0,sizeof(image_buf)); return -1; }
            size_t written = fwrite(image_buf.data,1,image_buf.used_len,image_file);
            fclose(image_file);
            if (written != image_buf.used_len) { memset(&image_buf,0,sizeof(image_buf)); return -1; }
            printf("[Image] Saved complete image: %s (%zu bytes)\n", image_buf.filename, written);
            memset(&image_buf,0,sizeof(image_buf));
        }
        return data_len;
    }
    return -1;
}
