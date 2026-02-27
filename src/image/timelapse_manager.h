#ifndef TIMELAPSE_MANAGER_H
#define TIMELAPSE_MANAGER_H

#include "protocol_defs.h"

// 延时摄影文件头结构 (sub-header for timelapse packets)
typedef struct {
    int8_t s8FileType;          // 文件类型：1:H265, 2:H264, 3:PCM, 4:G711A, 5:G711U, 6:AAC-LC
    int8_t s8FrameType;         // 帧类型: 1:I-Frame, 2:P-Frame, 0xFF:Empty Frame
    uint8_t u8Reserve[2];       // 保留
    int32_t s32FileLength;      // 文件大小
    int32_t s32TaskId;          // 任务ID，下载文件时使用
} TAG_PKG_FILE_HEADER_S;

// 处理延时摄影包（类似 handle_video_package）
int handle_timelapse_package(const unsigned char* package, int pkg_len);

#endif // TIMELAPSE_MANAGER_H
