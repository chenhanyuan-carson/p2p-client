#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "PPCS_API.h"
#include "video_decoder.h"
#include "video_display_gdi.h"
#include "control_panel.h"
#include "cJSON.h"

// 定义常量
#define CHANNEL 0                          // 通道号
#define BUFFER_SIZE 4096                   // 缓冲区大小
#define RECV_BUFFER_SIZE (1024*1024)       // 接收缓冲区大小（1MB）
#define CONFIG_FILE "config.conf"          // 配置文件
#define MAX_CONFIG_VALUE_LEN 256           // 配置值最大长度
#define MAX_JSON_LEN 2048                  // JSON 字符串最大长度

// 包标识和协议相关常量
#define PKG_IDENT 0x876e                   // 包标识 "IO"
#define PKG_JSON_PREFIX "#nsj"                  // 包前缀
#define PKG_VIDEO_PREFIX "$div"            // 视频包前缀
#define MAX_VIDEO_FRAME_SIZE 1024*1024     // 最大视频帧大小（1MB）

typedef enum {
    JSON_CMD_HEARTBEAT               = 0x01,  // 心跳包
    JSON_CMD_SETTINGS_GET            = 0x02,  // 获取设备设置
    JSON_CMD_REBOOT                  = 0x03,  // 重启设备
    JSON_CMD_RESET                   = 0x04,  // 恢复出厂设置
    JSON_CMD_OTA                     = 0x11,  // 固件升级
    JSON_CMD_OTA_PROGRESS            = 0x12,  // 固件升级进度
    JSON_CMD_SDCARD_GET              = 0x16,  // 获取 SD 卡信息
    JSON_CMD_SDCARD_FORMAT           = 0x17,  // 格式化 SD 卡
    JSON_CMD_SDCARD_POP              = 0x18,  // 弹出 SD 卡
    JSON_CMD_SDCARD_STATUS           = 0x19,  // SD 卡状态
    JSON_CMD_TIMEZONE_SET            = 0x51,  // 设置时区
    JSON_CMD_FILLLIGHT_SET           = 0x52,  // 补光灯设置
    JSON_CMD_LEDLIGHT_SET            = 0x53,  // LED 灯设置
    JSON_CMD_AI_SET                  = 0x54,  // AI 参数设置
    JSON_CMD_PRIVACY_SET             = 0x55,  // 隐私模式设置

    JSON_CMD_VIDEO_START             = 0x101, // 开始视频流
    JSON_CMD_VIDEO_STOP              = 0x102, // 停止视频流
    JSON_CMD_AUDIO_START             = 0x103, // 开始音频
    JSON_CMD_AUDIO_STOP              = 0x104, // 停止音频
    JSON_CMD_MIC_SET                 = 0x105, // 麦克风设置
    JSON_CMD_TALK_SET                = 0x106, // 对讲设置

    JSON_CMD_VIDEO_GET_PARAMS        = 0x111, // 获取视频参数
    JSON_CMD_VIDEO_RESOLUTION_SET    = 0x112, // 设置视频分辨率
    JSON_CMD_VIDEO_MIRROR_SET        = 0x113, // 设置视频镜像
    JSON_CMD_VIDEO_WATERMARK         = 0x114, // 视频水印设置
    JSON_CMD_VIDEOMODE               = 0x115, // 视频模式设置

    JSON_CMD_PLAYBACK_START          = 0x201, // 开始回放
    JSON_CMD_PLAYBACK_STOP           = 0x202, // 停止回放
    JSON_CMD_PLAYBACK_CTRL           = 0x203, // 回放控制（播放/暂停/快进等）
    JSON_CMD_RECORD_SETTINGS_GET     = 0x204, // 获取录制设置
    JSON_CMD_RECORD_SETTINGS_SET     = 0x205, // 设置录制参数
    JSON_CMD_RECORD_LIST_GET         = 0x206, // 获取录制文件列表

    JSON_CMD_TIMELAPSE_GET           = 0x301, // 获取延时摄影设置
    JSON_CMD_TIMELAPSE_SET           = 0x302, // 设置延时摄影参数
    JSON_CMD_TIMELAPSE_LIST          = 0x303, // 获取延时摄影片段列表
    JSON_CMD_TIMELAPSE_DOWNLOAD      = 0x304, // 下载延时摄影文件

    JSON_CMD_AUTOPHOTO_GET           = 0x311, // 获取自动拍照设置
    JSON_CMD_AUTOPHOTO_SET           = 0x312  // 设置自动拍照参数
} ELD_CMD_CODE;

// 全局序号管理（用于统一的 JSON seq / pkg id）
static unsigned short s_global_pkg_id = 1;
static unsigned short s_global_seq = 0;

// 网络读取线程 -> 完整包队列
typedef struct PackageNode {
    unsigned char* data;
    int len;
    struct PackageNode* next;
} PackageNode;

static PackageNode* g_pkg_head = NULL;
static PackageNode* g_pkg_tail = NULL;
static CRITICAL_SECTION g_pkg_cs;
static HANDLE g_pkg_event = NULL; // 用于唤醒
static HANDLE g_net_thread = NULL;
static volatile int g_net_thread_run = 0;

// 包头结构体
#pragma pack(1)
typedef struct {
    unsigned short s16PkgIdent;            // 包标识 PKG_IDENT(0x494f)
    unsigned short u16PkgLen;              // 包大小:JSON 数据包大小（不含前缀、包头、包尾）
    unsigned short u16PkgId;               // 包唯一标识
    unsigned short u16PkgIndex;            // 包序号(递减，0为结束包)
    unsigned short u16PkgKey;              // 包加密标识
    unsigned short u16PkgCmd;              // 包命令
    unsigned char  u8PkgSubHead;           // 1=含类型头信息, 0=不含类型头信息
    unsigned char  u8Reserve[3];           // 保留/对齐
    unsigned long long u64PkgUserData;     // 用户数据
} TAG_PKG_HEADER_S;

// 包尾结构体
typedef struct {
    unsigned char u8Zero;                  // 0值(必须)
    unsigned char u8Res;                   // 保留
    unsigned short u16Check;               // 校验
} TAG_PKG_TAIL_S;

// 视频包头结构体
typedef struct {
    char s8StreamType;                     // 流类型: 1:主码流. 2次码流. 3.录像流. 4.对讲 5.下载
    char s8EncodeType;                     // 编码类型: 1:H264. 2 H265. 3 JPEG
    char s8FrameType;                      // 帧类型: 1:I-Frame. 2：P-Frame
    char s8Ch;                             // 帧通道
    unsigned char u8Hour;
    unsigned char u8Minute;
    unsigned char u8Sec;
    unsigned char u8FrameRate;             // 视频帧率
    int s32FrameLen;                       // 帧大小
    unsigned short u16VideoWidth;          // 视频宽
    unsigned short u16VideoHeight;         // 视频高
    unsigned long long u64Pts;             // 帧时间戳
} TAG_PKG_VIDEO_HEADER_S;
#pragma pack()

// 配置结构体
typedef struct {
    char InitString[MAX_CONFIG_VALUE_LEN];
    char TargetDID[MAX_CONFIG_VALUE_LEN];
    char ServerString[MAX_CONFIG_VALUE_LEN];
    int MaxNumSess;
    int SessAliveSec;
    int UDPPort;
    int ConnectionMode;
    int ReadTimeout;
    char APILogFile[MAX_CONFIG_VALUE_LEN];
} Config;

// 获取连接模式字符串
const char* get_connection_mode(CHAR bMode) {
    switch(bMode) {
        case 0: return "LAN";
        case 1: return "LAN-TCP";
        case 2: return "P2P";
        case 3: return "Relay";
        case 4: return "TCP";
        case 5: return "RP2P";
        default: return "Unknown";
    }
}

// 获取错误信息
const char* get_error_msg(INT32 error_code) {
    switch(error_code) {
        case ERROR_PPCS_SUCCESSFUL:
            return "Operation successful";
        case ERROR_PPCS_NOT_INITIALIZED:
            return "PPCS not initialized";
        case ERROR_PPCS_ALREADY_INITIALIZED:
            return "PPCS already initialized";
        case ERROR_PPCS_TIME_OUT:
            return "Operation timeout";
        case ERROR_PPCS_INVALID_ID:
            return "Invalid device ID";
        case ERROR_PPCS_INVALID_PARAMETER:
            return "Invalid parameter";
        case ERROR_PPCS_DEVICE_NOT_ONLINE:
            return "Device not online";
        case ERROR_PPCS_SESSION_CLOSED_REMOTE:
            return "Session closed by remote";
        case ERROR_PPCS_SESSION_CLOSED_TIMEOUT:
            return "Session closed by timeout";
        case ERROR_PPCS_INVALID_SESSION_HANDLE:
            return "Invalid session handle";
        default:
            return "Unknown error";
    }
}

// 打印错误信息
void print_error(const char* function_name, INT32 error_code) {
    printf("[ERROR] %s: %s (code: %d)\n", function_name, get_error_msg(error_code), error_code);
}

// 从配置文件读取单个值
int read_config_value(const char* config_file, const char* key, char* value, int max_len) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        printf("[WARNING] Config file '%s' not found\n", config_file);
        return 0;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // 跳过注释和空行
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        // 查找 '=' 符号
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        
        // 提取 key
        int key_len = eq - line;
        if (strncmp(line, key, key_len) == 0 && strlen(key) == key_len) {
            // 提取 value
            char *val_start = eq + 1;
            int val_len = strlen(val_start);
            
            // 移除末尾的换行符和回车符
            while (val_len > 0 && (val_start[val_len - 1] == '\n' || val_start[val_len - 1] == '\r')) {
                val_len--;
            }
            
            // 限制长度
            if (val_len >= max_len) {
                val_len = max_len - 1;
            }
            
            // 复制值
            strncpy(value, val_start, val_len);
            value[val_len] = '\0';
            
            fclose(fp);
            return 1;
        }
    }
    
    fclose(fp);
    return 0;
}

// 初始化配置（带默认值）
void init_config(Config *config) {
    memset(config, 0, sizeof(Config));
    
    // 设置默认值
    strcpy(config->InitString, "");
    strcpy(config->TargetDID, "");
    strcpy(config->ServerString, "");
    config->MaxNumSess = 5;
    config->SessAliveSec = 6;
    config->UDPPort = 0;
    config->ConnectionMode = 0x7A;
    config->ReadTimeout = 5000;
    strcpy(config->APILogFile, "");
    
    // 从配置文件读取
    char value[MAX_CONFIG_VALUE_LEN-1];
    
    if (read_config_value(CONFIG_FILE, "InitString", value, sizeof(value))) {
        strncpy(config->InitString, value, sizeof(config->InitString) - 1);
    }
    
    if (read_config_value(CONFIG_FILE, "TargetDID", value, sizeof(value))) {
        strncpy(config->TargetDID, value, sizeof(config->TargetDID) - 1);
    }
    
    if (read_config_value(CONFIG_FILE, "ServerString", value, sizeof(value))) {
        strncpy(config->ServerString, value, sizeof(config->ServerString) - 1);
    }
    
    if (read_config_value(CONFIG_FILE, "MaxNumSess", value, sizeof(value))) {
        config->MaxNumSess = atoi(value);
    }
    
    if (read_config_value(CONFIG_FILE, "SessAliveSec", value, sizeof(value))) {
        config->SessAliveSec = atoi(value);
    }
    
    if (read_config_value(CONFIG_FILE, "UDPPort", value, sizeof(value))) {
        config->UDPPort = atoi(value);
    }
    
    if (read_config_value(CONFIG_FILE, "ConnectionMode", value, sizeof(value))) {
        config->ConnectionMode = (int)strtol(value, NULL, 0);  // 支持 0x7A 格式
    }
    
    if (read_config_value(CONFIG_FILE, "ReadTimeout", value, sizeof(value))) {
        config->ReadTimeout = atoi(value);
    }
    
    if (read_config_value(CONFIG_FILE, "APILogFile", value, sizeof(value))) {
        strncpy(config->APILogFile, value, sizeof(config->APILogFile) - 1);
    }
}

// 验证配置
int validate_config(Config *config) {
    if (strlen(config->InitString) == 0) {
        printf("[ERROR] InitString not configured in %s\n", CONFIG_FILE);
        return 0;
    }
    
    if (strlen(config->TargetDID) == 0) {
        printf("[ERROR] TargetDID not configured in %s\n", CONFIG_FILE);
        return 0;
    }
    
    if (config->MaxNumSess < 1 || config->MaxNumSess > 512) {
        printf("[WARNING] MaxNumSess out of range (1-512), using default 5\n");
        config->MaxNumSess = 5;
    }
    
    if (config->SessAliveSec < 6 || config->SessAliveSec > 30) {
        printf("[WARNING] SessAliveSec out of range (6-30), using default 6\n");
        config->SessAliveSec = 6;
    }
    
    return 1;
}

// 打印配置信息
void print_config(Config *config) {
    printf("[Configuration Loaded]\n");
    printf("  InitString: %s\n", config->InitString);
    printf("  TargetDID: %s\n", config->TargetDID);
    printf("  ServerString: %s\n", strlen(config->ServerString) > 0 ? config->ServerString : "(default server)");
    printf("  MaxNumSess: %d\n", config->MaxNumSess);
    printf("  SessAliveSec: %d\n", config->SessAliveSec);
    printf("  ConnectionMode: 0x%02X\n", config->ConnectionMode);
    printf("  ReadTimeout: %d ms\n", config->ReadTimeout);
    if (strlen(config->APILogFile) > 0) {
        printf("  APILogFile: %s\n", config->APILogFile);
    }
    printf("\n");
}

// 打印 API 版本信息
void print_api_info() {
    UINT32 version = PPCS_GetAPIVersion();
    printf("=====================================\n");
    printf("PPCS API Version: %d.%d.%d.%d\n", 
           (version >> 24) & 0xFF,
           (version >> 16) & 0xFF,
           (version >> 8) & 0xFF,
           version & 0xFF);
    printf("=====================================\n\n");
}

// 打印网络信息
void print_network_info(st_PPCS_NetInfo* net_info) {
    printf("[Network Information]\n");
    printf("  Internet Reachable: %s\n", net_info->bFlagInternet ? "YES" : "NO");
    printf("  Host Resolved: %s\n", net_info->bFlagHostResolved ? "YES" : "NO");
    printf("  Server Hello: %s\n", net_info->bFlagServerHello ? "YES" : "NO");
    printf("  NAT Type: ");
    switch(net_info->NAT_Type) {
        case 0: printf("Unknown\n"); break;
        case 1: printf("IP-Restricted Cone\n"); break;
        case 2: printf("Port-Restricted Cone\n"); break;
        case 3: printf("Symmetric\n"); break;
        default: printf("Unknown (%d)\n", net_info->NAT_Type); break;
    }
    printf("  LAN IP: %s\n", net_info->MyLanIP);
    printf("  WAN IP: %s\n", net_info->MyWanIP);
    printf("\n");
}

// 单个视频流的信息结构
typedef struct {
    int stream_type;                        // 流类型: 1=主码流, 2=次码流, 3=录像流
    FILE* output_file;                      // 文件输出
    int frame_count;                        // 帧计数
    unsigned long long total_bytes;         // 总字节数
    
    // 解码和显示
    VideoDecoder* decoder;
    VideoDisplay* display;
    int codec_type;                         // 编码类型
    int video_width;                        // 原始宽度
    int video_height;                       // 原始高度
    TAG_PKG_VIDEO_HEADER_S last_header;     // 最后一个视频头
} VideoStream;

// 视频流管理器 - 管理多个流
typedef struct {
    VideoStream* streams[5];                // 支持5种流类型 (索引对应 stream_type)
    int active_stream_count;                // 活跃流数量
} VideoStreamManager;

// 应用程序上下文 - 用于按钮回调
typedef struct {
    INT32 session_handle;
    VideoStreamManager* video_mgr;
    int live_started;
    int playback_started;
} AppContext;

// 获取流类型的名称
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

// 创建单个视频流
VideoStream* create_video_stream(int stream_type, const char* output_file_prefix) {
    VideoStream* stream = (VideoStream*)malloc(sizeof(VideoStream));
    if (!stream) return NULL;
    
    memset(stream, 0, sizeof(VideoStream));
    stream->stream_type = stream_type;
    
    // 根据流类型创建不同的输出文件
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_stream%d.h265", 
             output_file_prefix, stream_type);
    
    stream->output_file = fopen(filename, "wb");
    if (!stream->output_file) {
        printf("[Stream%d] Warning: Failed to open output file: %s\n", 
               stream_type, filename);
    } else {
        printf("[Stream%d] Output file created: %s\n", stream_type, filename);
    }
    
    return stream;
}

// 销毁单个视频流
void destroy_video_stream(VideoStream* stream) {
    if (!stream) return;
    
    printf("[Stream%d] Destroying stream (%s)...\n", 
           stream->stream_type, get_stream_type_name(stream->stream_type));
    
    if (stream->display) {
        video_display_destroy(stream->display);
    }
    
    if (stream->decoder) {
        video_decoder_destroy(stream->decoder);
    }
    
    if (stream->output_file) {
        fclose(stream->output_file);
    }
    
    printf("[Stream%d] Statistics: %d frames, %.2f MB\n",
           stream->stream_type, stream->frame_count,
           (float)stream->total_bytes / (1024 * 1024));
    
    free(stream);
}

// 创建视频流管理器
VideoStreamManager* create_video_stream_manager(const char* output_file_prefix) {
    VideoStreamManager* mgr = (VideoStreamManager*)malloc(sizeof(VideoStreamManager));
    if (!mgr) return NULL;
    
    memset(mgr, 0, sizeof(VideoStreamManager));
    mgr->active_stream_count = 0;
    
    printf("[VideoMgr] Stream manager created\n");
    return mgr;
}

// 获取或创建指定类型的流
VideoStream* get_or_create_stream(VideoStreamManager* mgr, int stream_type, 
                                  const char* output_file_prefix) {
    if (!mgr || stream_type < 1 || stream_type > 5) {
        return NULL;
    }
    
    // 检查流是否已存在
    if (mgr->streams[stream_type - 1] != NULL) {
        return mgr->streams[stream_type - 1];
    }
    
    // 创建新流
    VideoStream* stream = create_video_stream(stream_type, output_file_prefix);
    if (stream) {
        mgr->streams[stream_type - 1] = stream;
        mgr->active_stream_count++;
        printf("[VideoMgr] Created stream type %d (%s), total active: %d\n",
               stream_type, get_stream_type_name(stream_type), 
               mgr->active_stream_count);
    }
    
    return stream;
}

// 销毁视频流管理器
void destroy_video_stream_manager(VideoStreamManager* mgr) {
    if (!mgr) return;
    
    printf("[VideoMgr] Destroying stream manager...\n");
    
    // 销毁所有活跃的流
    for (int i = 0; i < 5; i++) {
        if (mgr->streams[i]) {
            destroy_video_stream(mgr->streams[i]);
            mgr->streams[i] = NULL;
        }
    }
    
    free(mgr);
    printf("[VideoMgr] Stream manager destroyed\n");
}

// 保存视频帧到文件
void save_video_frame(VideoStream* stream, 
                     const unsigned char* frame_data,
                     int frame_size) {
    if (!stream || !stream->output_file) return;
    
    size_t written = fwrite(frame_data, 1, frame_size, stream->output_file);
    if (written != frame_size) {
        printf("[Stream%d] ERROR: Failed to write video frame\n", stream->stream_type);
        return;
    }
    
    stream->total_bytes += frame_size;
}

// 视频帧解码回调函数 - 在解码完成时自动调用
void on_frame_decoded(VideoFrame* frame, void* user_data) {
    VideoStream* stream = (VideoStream*)user_data;
    if (!stream || !stream->display) return;
    
    // 每30帧打印一次信息，减少日志输出
    if (stream->frame_count % 30 == 0) {
        printf("[Stream%d] Decoded frame: %dx%d, PTS: %lld\n", 
               stream->stream_type, frame->width, frame->height, frame->pts);
    }
    
    // 渲染视频帧
    int ret = video_display_render(stream->display, frame);
    if (ret < 0 && stream->frame_count % 30 == 0) {
        printf("[Stream%d] Failed to render frame: %d\n", stream->stream_type, ret);
    }
    
    // 处理窗口事件
    if (!video_display_poll_events(stream->display)) {
        printf("[Stream%d] Display window closed by user\n", stream->stream_type);
    }
}

// 将完整包入队（网络线程调用）
static void push_package_to_queue(unsigned char* data, int len) {
    PackageNode* node = (PackageNode*)malloc(sizeof(PackageNode));
    if (!node) {
        free(data);
        return;
    }
    node->data = data;
    node->len = len;
    node->next = NULL;

    EnterCriticalSection(&g_pkg_cs);
    if (g_pkg_tail) {
        g_pkg_tail->next = node;
        g_pkg_tail = node;
    } else {
        g_pkg_head = g_pkg_tail = node;
    }
    LeaveCriticalSection(&g_pkg_cs);

    // 发信号
    if (g_pkg_event) SetEvent(g_pkg_event);
}

// 从队列弹出一个包，返回节点（caller 负责 free node->data 和 free(node)）
static PackageNode* pop_package_from_queue(void) {
    PackageNode* node = NULL;
    EnterCriticalSection(&g_pkg_cs);
    if (g_pkg_head) {
        node = g_pkg_head;
        g_pkg_head = g_pkg_head->next;
        if (!g_pkg_head) g_pkg_tail = NULL;
    }
    LeaveCriticalSection(&g_pkg_cs);
    return node;
}

// 网络读取线程：阻塞读、组装完整包并入队
static DWORD WINAPI network_reader_thread(LPVOID lpParam) {
    INT32 session_handle = (INT32)(intptr_t)lpParam;
    unsigned char* recv_buffer = (unsigned char*)malloc(RECV_BUFFER_SIZE);
    if (!recv_buffer) return 0;
    int buffer_data_len = 0;

    g_net_thread_run = 1;

    while (g_net_thread_run) {
        unsigned char temp_buffer[BUFFER_SIZE];
        INT32 read_len = sizeof(temp_buffer);
        INT32 ret = PPCS_Read(session_handle, CHANNEL, (char*)temp_buffer, &read_len, 500);
        // 如果数据小于 BUFFER_SIZE, 返回的是 -3，所以 if (ret == ERROR_PPCS_SUCCESSFUL && read_len > 0) 这是不对的
        if ((ret == ERROR_PPCS_SUCCESSFUL || ret == ERROR_PPCS_TIME_OUT) && read_len > 0) {
            // 追加到本地重组缓冲区
            if (buffer_data_len + read_len <= RECV_BUFFER_SIZE) {
                memcpy(recv_buffer + buffer_data_len, temp_buffer, read_len);
                buffer_data_len += read_len;
            } else {
                printf("[WARNING] Receive buffer overflow, discarding data\n");
                // 缓冲区被填满，重置以避免溢出
                buffer_data_len = 0;
                continue;
            }

            // 尝试解析完整的包并入队
            while (buffer_data_len >= 40) {
                int is_json = (memcmp(recv_buffer, PKG_JSON_PREFIX, 4) == 0);
                int is_video = (memcmp(recv_buffer, PKG_VIDEO_PREFIX, 4) == 0);
                if (!is_json && !is_video) {
                    // 跳过无效字节
                    printf("[WARNING] Invalid package prefix, skipping 1 byte\n");
                    memmove(recv_buffer, recv_buffer + 1, buffer_data_len - 1);
                    buffer_data_len--;
                    continue;
                }

                if (buffer_data_len < 4 + sizeof(TAG_PKG_HEADER_S)) break;
                TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(recv_buffer + 4);
                // 4+24+4 = 32
                int pkg_len = 4 + sizeof(TAG_PKG_HEADER_S) + header->u16PkgLen + sizeof(TAG_PKG_TAIL_S);
                if (pkg_len <= 32 || pkg_len > RECV_BUFFER_SIZE) {
                    // 非法包长度，跳过前4字节
                    printf("[WARNING] Invalid package length: %d (header->u16PkgLen=%d)\n", pkg_len, header->u16PkgLen);
                    memmove(recv_buffer, recv_buffer + 4, buffer_data_len - 4);
                    buffer_data_len -= 4;
                    continue;
                }

                if (buffer_data_len < pkg_len) break; // 等待更多数据

                // 完整包
                unsigned char* pkg = (unsigned char*)malloc(pkg_len);
                if (pkg) {
                    memcpy(pkg, recv_buffer, pkg_len);
                    push_package_to_queue(pkg, pkg_len);
                }

                // 移除已消费的数据
                memmove(recv_buffer, recv_buffer + pkg_len, buffer_data_len - pkg_len);
                buffer_data_len -= pkg_len;
            }
        } else {
            // PPCS_Read 出错或超时（ret<0 或 read_len==0），继续循环
            // 如果遇到严重错误可以考虑退出线程
        }
    }

    free(recv_buffer);
    return 0;
}

// 函数声明
int build_command_package(const char* json_data, unsigned char* package, int max_len,
                         unsigned short pkg_id, unsigned short pkg_cmd);
int handle_command_package(const unsigned char* package, int pkg_len);
// 统一封装：构建包并发送（封装 build_command_package + PPCS_Write）
int send_command(INT32 session_handle, const char* json_data, unsigned short pkg_id, unsigned short pkg_cmd) {
    unsigned char package[BUFFER_SIZE];
    int pkg_len = build_command_package(json_data, package, sizeof(package), pkg_id, pkg_cmd);
    if (pkg_len < 0) {
        printf("[Command] Failed to build package (pkg_id=%d, pkg_cmd=0x%04X)\n", pkg_id, pkg_cmd);
        return -1;
    }

    INT32 ret = PPCS_Write(session_handle, CHANNEL, (char*)package, pkg_len);
    if (ret < 0) {
        print_error("PPCS_Write", ret);
        return -1;
    }

    printf("[Command] Sent: %d bytes (pkg_id=%d, pkg_cmd=0x%04X)\n", ret, pkg_id, pkg_cmd);
    return 0;
}

// 直播按钮回调
void on_live_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || ctx->live_started) {
        printf("[App] Live already started\n");
        return;
    }
    
    printf("[App] Starting live stream...\n");
    // 统一实现：构建 JSON 并通过 send_command 发送
    char json_request[MAX_JSON_LEN];
    // 使用空 id 与固定 user（与之前实现保持一致）
    const char* id = "Android_1c775ac30545f25a";
    const char* user = "29566628-5071-47e7-b5f5-9cc3849c9ade";
    snprintf(json_request, sizeof(json_request),
             "{"
             "\"version\":\"1.0\","
             "\"ack\":false,"
             "\"seq\":%d,"
             "\"cmd\":%d,"
             "\"def\":\"JSON_CMD_VIDEO_START\","
             "\"id\":\"%s\","
             "\"user\":\"%s\""
             "}",
             s_global_seq++, 0x101, id, user);

    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_VIDEO_START) == 0) {
        ctx->live_started = 1;
        printf("[App] Live stream started\n");
    } else {
        printf("[App] Failed to start live stream\n");
    }
}

// 录像回放按钮回调
void on_playback_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx || ctx->playback_started) {
        printf("[App] Playback already started\n");
        return;
    }
    
    printf("[App] Starting playback stream...\n");
    // 统一实现：构建 JSON 并通过 send_command 发送（回放）
    char json_request[MAX_JSON_LEN];
    const char* id = "Android_1c775ac30545f25a";
    const char* user = "29566628-5071-47e7-b5f5-9cc3849c9ade";
    // 示例时间窗口（可按需修改或扩展为 UI 输入）
    const char* start_time = "2024-01-01T12:00:00Z";
    const char* end_time = "2024-01-01T12:30:00Z";
    snprintf(json_request, sizeof(json_request),
             "{"
             "\"version\":\"1.0\","
             "\"ack\":false,"
             "\"seq\":%d,"
             "\"cmd\":%d,"
             "\"def\":\"JSON_CMD_PLAYBACK_START\","
             "\"id\":\"%s\","
             "\"user\":\"%s\","
             "\"data\":{\"start_time\":\"%s\",\"end_time\":\"%s\"}"
             "}",
             s_global_seq++, 0x201, id, user, start_time, end_time);

    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_PLAYBACK_START) == 0) {
        ctx->playback_started = 1;
        printf("[App] Playback stream started\n");
    } else {
        printf("[App] Failed to start playback stream\n");
    }
}

// 录像列表按钮回调
void on_record_list_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) return;
    printf("[App] Querying record list (UI)...\n");
    // 构造JSON请求，字段按用户提供的协议
    char json_request[MAX_JSON_LEN];
    const char* id = "Android_1c775ac30545f25a";
    const char* user = "29566628-5071-47e7-b5f5-9cc3849c9ade";
    // 使用字符串时间格式 "YYYY-MM-DD HH:MM:SS"
    const char* start_time_str = "2025-12-16 00:00:00";
    const char* end_time_str   = "2025-12-16 14:50:00"; // 示例结束时间，可按需调整
    snprintf(json_request, sizeof(json_request),
        "{"
        "\"version\":\"1.0\","
        "\"ack\":false,"
        "\"seq\":%d,"
        "\"cmd\":%d,"
        "\"def\":\"JSON_CMD_RECORD_LIST_GET\","
        "\"id\":\"%s\","
        "\"user\":\"%s\","
        "\"data\":{\"startTime\":\"%s\",\"endTime\":\"%s\"}"
        "}",
        s_global_seq++, 0x207, id, user, start_time_str, end_time_str);

    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, 0x207) != 0) {
        printf("[App] Failed to send record list command\n");
        return;
    }
    printf("[App] Record list command sent\n");
}

// 处理视频数据包 - 支持多流分发
int handle_video_package(VideoStreamManager* mgr,
                        const unsigned char* package,
                        int pkg_len) {
    if (!mgr) return -1;
    
    if (pkg_len < 4 + sizeof(TAG_PKG_HEADER_S) + sizeof(TAG_PKG_TAIL_S)) {
        printf("[Video] Package too small\n");
        return -1;
    }
    
    int offset = 4;  // 跳过 $div 前缀
    
    // 解析包头
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset);
    offset += sizeof(TAG_PKG_HEADER_S);
    // 打印包头信息（每30个包打印一次，减少日志）
        #if 0
        static int header_log_counter = 0;
        if (header_log_counter++ % 30 == 0) {
            printf("[Video] Package Header:\n");
            printf("  PkgIdent: 0x%04X\n", header->s16PkgIdent);
            printf("  PkgLen: %d\n", header->u16PkgLen);
            printf("  PkgId: %d\n", header->u16PkgId);
            printf("  PkgIndex: %d\n", header->u16PkgIndex);
            printf("  PkgKey: %d\n", header->u16PkgKey);
            printf("  PkgCmd: 0x%04X\n", header->u16PkgCmd);
            printf("  PkgSubHead: %d\n", header->u8PkgSubHead);
            printf("  PkgUserData: %llu\n", header->u64PkgUserData);
        }
        #endif
    
    // 分片重组缓存结构
    typedef struct {
        unsigned short pkg_id;
        int stream_type;
        int total_len;
        int used_len;
        unsigned char data[MAX_VIDEO_FRAME_SIZE];
        VideoStream* stream;
        TAG_PKG_VIDEO_HEADER_S video_header;
        int valid;
    } FrameBuffer;
    static FrameBuffer frame_buf = {0};
    VideoStream* stream = NULL;
    int stream_type = -1;

    if (header->u8PkgSubHead == 1) {
        // 新帧起始，重置缓存
        if (pkg_len < offset + sizeof(TAG_PKG_VIDEO_HEADER_S)) {
            printf("[Video] Package incomplete\n");
            return -1;
        }
        TAG_PKG_VIDEO_HEADER_S* video_header = (TAG_PKG_VIDEO_HEADER_S*)(package + offset);
        offset += sizeof(TAG_PKG_VIDEO_HEADER_S);
        stream_type = video_header->s8StreamType;
        // 打印
        static int log_counter = 0;
        if (log_counter++ % 10 == 0) {
            printf("[Video] Stream:%d(%s), Encode:%d, Frame:%d, Res:%dx%d, FPS:%d, Length:%d\n",
                stream_type, get_stream_type_name(stream_type),
                video_header->s8EncodeType, video_header->s8FrameType,
                video_header->u16VideoWidth, video_header->u16VideoHeight,
                video_header->u8FrameRate, video_header->s32FrameLen);
        }
        stream = get_or_create_stream(mgr, stream_type, "output_video");
        if (!stream) {
            printf("[Video] Failed to get stream for type %d\n", stream_type);
            return -1;
        }
        memcpy(&stream->last_header, video_header, sizeof(TAG_PKG_VIDEO_HEADER_S));
        if (!stream->decoder && video_header->s8EncodeType > 0) {
            stream->codec_type = video_header->s8EncodeType;
            stream->video_width = video_header->u16VideoWidth;
            stream->video_height = video_header->u16VideoHeight;
            int display_width, display_height;
            /*if (stream->video_width > 1920) {
                display_width = 1920; 
                display_height = 1080;
                printf("[Stream%d] 4K detected, scaling to 1080p\n", stream_type);
            } else */
            if (stream->video_width > 1280) {
                display_width = 1280; 
                display_height = 720;
                printf("[Stream%d] 1080p detected, scaling to 720p\n", stream_type);
            } else {
                display_width = stream->video_width; display_height = stream->video_height;
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
            printf("[Stream%d] Creating decoder (type: %d) with callback...\n", stream_type, stream->codec_type);
            stream->decoder = video_decoder_create(stream->codec_type, on_frame_decoded, stream);
            if (!stream->decoder) {
                printf("[Stream%d] Warning: Failed to create decoder\n", stream_type);
            } else {
                if (display_width != stream->video_width || display_height != stream->video_height) {
                    printf("[Stream%d] Setting decoder scale: %dx%d -> %dx%d\n", stream_type, stream->video_width, stream->video_height, display_width, display_height);
                    int ret = video_decoder_set_scale(stream->decoder, display_width, display_height);
                    if (ret < 0) {
                        printf("[Stream%d] Warning: Failed to set scale\n", stream_type);
                    }
                }
            }
        }
        // 初始化分片缓存
        frame_buf.pkg_id = header->u16PkgId;
        frame_buf.stream_type = stream_type;
        frame_buf.total_len = 0;
        frame_buf.used_len = 0;
        frame_buf.stream = stream;
        memcpy(&frame_buf.video_header, video_header, sizeof(TAG_PKG_VIDEO_HEADER_S));
        frame_buf.valid = 1;
        // 累加本分片
        int video_data_len = pkg_len - offset - sizeof(TAG_PKG_TAIL_S);
        if (video_data_len > 0 && frame_buf.used_len + video_data_len <= MAX_VIDEO_FRAME_SIZE) {
            memcpy(frame_buf.data + frame_buf.used_len, package + offset, video_data_len);
            frame_buf.used_len += video_data_len;
        }
        // 如果是最后一片，送解码器
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                save_video_frame(frame_buf.stream, frame_buf.data, frame_buf.used_len);
                frame_buf.stream->frame_count++;
                frame_buf.stream->total_bytes += frame_buf.used_len;
                uint64_t pts = frame_buf.video_header.u64Pts;
                int ret = video_decoder_decode(frame_buf.stream->decoder, frame_buf.data, frame_buf.used_len, pts);
                if (ret < 0) {
                    printf("[Stream%d] Warning: Decode error %d\n", frame_buf.stream->stream_type, ret);
                }
            }
            frame_buf.valid = 0;
        }
        return video_data_len;
    } else {
        // 分片包，必须PkgId匹配才累加
        if (!frame_buf.valid || header->u16PkgId != frame_buf.pkg_id) {
            static int skip_count = 0;
            if (skip_count++ % 30 == 0) {
                printf("[Video] No matching frame buffer for data-only package (PkgId=%d, skipped %d)\n", header->u16PkgId, skip_count);
            }
            return -1;
        }
        int video_data_len = pkg_len - offset - sizeof(TAG_PKG_TAIL_S);
        if (video_data_len > 0 && frame_buf.used_len + video_data_len <= MAX_VIDEO_FRAME_SIZE) {
            memcpy(frame_buf.data + frame_buf.used_len, package + offset, video_data_len);
            frame_buf.used_len += video_data_len;
        }
        // 如果是最后一片，送解码器
        if (header->u16PkgIndex == 0) {
            if (frame_buf.valid && frame_buf.used_len > 0) {
                save_video_frame(frame_buf.stream, frame_buf.data, frame_buf.used_len);
                frame_buf.stream->frame_count++;
                frame_buf.stream->total_bytes += frame_buf.used_len;
                uint64_t pts = frame_buf.video_header.u64Pts;
                int ret = video_decoder_decode(frame_buf.stream->decoder, frame_buf.data, frame_buf.used_len, pts);
                if (ret < 0) {
                    printf("[Stream%d] Warning: Decode error %d\n", frame_buf.stream->stream_type, ret);
                }
            }
            frame_buf.valid = 0;
        }
        return video_data_len;
    }
    
    // ...existing code...
}

// 处理命令响应包
int handle_command_package(const unsigned char* package,
                          int pkg_len) {
    if (pkg_len < 4 + sizeof(TAG_PKG_HEADER_S)) {
        printf("[Command] Package too small\n");
        return -1;
    }
    
    int offset = 4;  // 跳过 #nsj 前缀
    
    // 解析包头
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset);
    
    printf("[Command] Package Header:\n");
    printf("  PkgIdent: 0x%04X\n", header->s16PkgIdent);
    printf("  PkgLen: %d\n", header->u16PkgLen);
    printf("  PkgId: %d\n", header->u16PkgId);
    printf("  PkgIndex: %d\n", header->u16PkgIndex);
    printf("  PkgKey: %d\n", header->u16PkgKey);
    printf("  PkgCmd: 0x%04X\n", header->u16PkgCmd);
    printf("  PkgSubHead: %d\n", header->u8PkgSubHead);
    printf("  PkgUserData: %llu\n", header->u64PkgUserData);
    
    offset += sizeof(TAG_PKG_HEADER_S);
    
    int json_len = header->u16PkgLen;
    
    // 提取JSON
    char json_response[MAX_JSON_LEN];
    if (json_len > MAX_JSON_LEN - 1) {
        printf("[Command] JSON too large\n");
        return -1;
    }
    
    memcpy(json_response, package + offset, json_len);
    json_response[json_len] = '\0';
    
    printf("[Response] %s\n", json_response);
    
    // 如果是录像列表响应（cmd=0x207 或 包体包含 JSON_CMD_RECORD_LIST_GET），用 cJSON 解析新版格式
    if (header->u16PkgCmd == 0x207 || strstr(json_response, "JSON_CMD_RECORD_LIST_GET") != NULL) {
        printf("[RecordList] Parsing record list response (cJSON)...\n");
        // 解析 JSON
        cJSON *root = cJSON_Parse(json_response);
        if (!root) {
            printf("[RecordList] Invalid JSON!\n");
            return -1;
        }
        // 兼容 data/recordlist 或 data/record_list
        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (!data) {
            printf("[RecordList] No 'data' object in response\n");
            cJSON_Delete(root);
            return -1;
        }
        // 支持 recordlist/record_list
        cJSON *recordlist = cJSON_GetObjectItemCaseSensitive(data, "recordList");
        if (!recordlist || !cJSON_IsArray(recordlist)) {
            printf("[RecordList] No valid 'recordlist' array in response\n");
            cJSON_Delete(root);
            return -1;
        }
        int idx = 0;
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, recordlist) {
            // 支持 startTime/start_time, endTime/end_time, recType/record_type
            cJSON *start_time = cJSON_GetObjectItemCaseSensitive(item, "startTime");
            cJSON *end_time = cJSON_GetObjectItemCaseSensitive(item, "endTime");
            cJSON *rec_type = cJSON_GetObjectItemCaseSensitive(item, "recType");
            cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "size");
            cJSON *frame_rate = cJSON_GetObjectItemCaseSensitive(item, "frameRate");
            cJSON *code_type = cJSON_GetObjectItemCaseSensitive(item, "codeType");

            if (start_time == NULL || end_time == NULL || rec_type == NULL 
                || size == NULL || frame_rate == NULL || code_type == NULL) {
                // 尝试下划线命名
                printf("[RecordList][%d] Trying alternative field names...\n", idx);
            } else {
                    printf("[RecordList][%d] startTime=%s, endTime=%s, size=%u, recType=%d, frameRate=%d, codeType=%d\n",
                    idx++,
                    start_time && cJSON_IsString(start_time) ? start_time->valuestring : "",
                    end_time && cJSON_IsString(end_time) ? end_time->valuestring : "",
                    size && cJSON_IsNumber(size) ? (unsigned int)size->valuedouble : 0,
                    rec_type && cJSON_IsNumber(rec_type) ? rec_type->valueint : -1,
                    frame_rate && cJSON_IsNumber(frame_rate) ? frame_rate->valueint : -1,
                    code_type && cJSON_IsNumber(code_type) ? code_type->valueint : -1
                );         
            }
        }
        cJSON_Delete(root);
        return 0;
    }

    // 检查响应是否成功
    if (strstr(json_response, "\"code\":200") != NULL) {
        printf("[SUCCESS] Command executed successfully\n");
        return 0;
    } else if (strstr(json_response, "\"ack\":true") != NULL) {
        printf("[INFO] Command acknowledged\n");
        return 0;
    }

    return 0;
}

// 计算校验码
unsigned short calculate_checksum(const unsigned char* data, int len) {
    unsigned short checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += data[i];
    }
    return checksum;
}

// 构建 JSON 命令请求包
int build_command_package(const char* json_data, unsigned char* package, int max_len,
                          unsigned short pkg_id, unsigned short pkg_cmd) {
    int json_len = strlen(json_data);
    
    // 计算总长度：前缀(4) + 包头(32) + JSON + 包尾(4)
    int total_len = 4 + sizeof(TAG_PKG_HEADER_S) + json_len + sizeof(TAG_PKG_TAIL_S);
    
    if (total_len > max_len) {
        printf("[ERROR] Package too large\n");
        return -1;
    }
    
    int offset = 0;
    
    // 添加前缀
    memcpy(package + offset, PKG_JSON_PREFIX, 4);
    offset += 4;
    
    // 构建包头
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset);
    memset(header, 0, sizeof(TAG_PKG_HEADER_S));
    
    header->s16PkgIdent = PKG_IDENT;
    header->u16PkgLen = json_len;
    header->u16PkgId = pkg_id;
    header->u16PkgIndex = 0;               // 结束包
    header->u16PkgKey = 0;                 // 不加密
    header->u16PkgCmd = pkg_cmd;
    header->u8PkgSubHead = 0;              // 不含类型头信息
    
    offset += sizeof(TAG_PKG_HEADER_S);
    
    // 添加 JSON 数据
    memcpy(package + offset, json_data, json_len);
    offset += json_len;
    
    // 构建包尾
    TAG_PKG_TAIL_S* tail = (TAG_PKG_TAIL_S*)(package + offset);
    tail->u8Zero = 0;
    tail->u8Res = 0;
    tail->u16Check = calculate_checksum(package, offset);
    
    offset += sizeof(TAG_PKG_TAIL_S);
    
    return offset;
}

// 解析 JSON 命令响应包
int parse_command_response(const unsigned char* package, int pkg_len, char* json_out, int max_json_len) {
    if (pkg_len < 4 + sizeof(TAG_PKG_HEADER_S) + sizeof(TAG_PKG_TAIL_S)) {
        printf("[ERROR] Package too small\n");
        return -1;
    }
    
    int offset = 0;
    
    // 检查前缀
    if (memcmp(package + offset, PKG_JSON_PREFIX, 4) != 0) {
        printf("[ERROR] Invalid package prefix\n");
        return -1;
    }
    offset += 4;
    
    // 解析包头
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset);
    
    if (header->s16PkgIdent != PKG_IDENT) {
        printf("[ERROR] Invalid package ident\n");
        return -1;
    }
    
    int json_len = header->u16PkgLen;
    offset += sizeof(TAG_PKG_HEADER_S);
    
    // 提取 JSON 数据
    if (json_len > max_json_len - 1) {
        printf("[ERROR] JSON too large\n");
        return -1;
    }
    
    memcpy(json_out, package + offset, json_len);
    json_out[json_len] = '\0';
    offset += json_len;
    
    // 验证包尾
    TAG_PKG_TAIL_S* tail = (TAG_PKG_TAIL_S*)(package + offset);
    if (tail->u8Zero != 0) {
        printf("[WARNING] Invalid package tail zero byte\n");
    }
    
    return 0;
}

// 连接到P2P设备 - 只处理连接，返回session_handle
INT32 connect_to_device(const char* target_did, Config *config) {
    INT32 ret;
    INT32 session_handle;
    st_PPCS_Session session_info;
    
    printf("\n[Connecting to Device]\n");
    printf("Target DID: %s\n", target_did);
    printf("================================================\n\n");
    
    // 使用配置的连接模式
    CHAR bEnableLanSearch = (CHAR)config->ConnectionMode;
    
    printf("Calling PPCS_ConnectByServer()...\n");
    printf("Connection Mode: 0x%02X\n\n", bEnableLanSearch);
    
    // 连接到服务器
    session_handle = PPCS_ConnectByServer(target_did, bEnableLanSearch, 
                                          config->UDPPort, config->ServerString);
    
    if (session_handle < 0) {
        print_error("PPCS_ConnectByServer", session_handle);
        return -1;
    }
    
    printf("Connected successfully! Session Handle: %d\n\n", session_handle);
    
    // 检查会话信息
    ret = PPCS_Check(session_handle, &session_info);
    if (ret == ERROR_PPCS_SUCCESSFUL) {
        printf("[Session Information]\n");
        printf("  Socket: %d\n", session_info.Skt);
        printf("  Device ID: %s\n", session_info.DID);
        printf("  Role: %s\n", session_info.bCorD ? "Device" : "Client");
        printf("  Connection Mode: %s\n", get_connection_mode(session_info.bMode));
        printf("  Connection Time: %u seconds ago\n", session_info.ConnectTime);
        printf("\n");
    } else {
        print_error("PPCS_Check", ret);
        PPCS_Close(session_handle);
        return -1;
    }
    
    return session_handle;
}

// 主函数
int main(int argc, char* argv[]) {
    INT32 ret;
    st_PPCS_NetInfo net_info;
    Config config;
    char json_init_string[MAX_JSON_LEN];
    
    printf("\n");
    printf("*****************************************************\n");
    printf("*           P2P Simple Client (Based on PPCS API)\n");
    printf("*****************************************************\n\n");
    
    // 打印 API 版本信息
    print_api_info();
    
    // 读取配置文件
    printf("Loading configuration from %s...\n", CONFIG_FILE);
    init_config(&config);
    
    // 验证配置
    if (!validate_config(&config)) {
        return -1;
    }
    
    // 打印配置信息
    print_config(&config);
    
    // 构建 JSON 初始化字符串（根据最新 API 版本）
    // 支持 API version >= 4.2.0 的 JSON 格式
    if (strlen(config.APILogFile) > 0) {
        snprintf(json_init_string, sizeof(json_init_string),
                 "{\"InitString\":\"%s\",\"MaxNumSess\":%d,\"SessAliveSec\":%d,\"APILogFile\":\"%s\"}",
                 config.InitString, config.MaxNumSess, config.SessAliveSec, config.APILogFile);
    } else {
        snprintf(json_init_string, sizeof(json_init_string),
                 "{\"InitString\":\"%s\",\"MaxNumSess\":%d,\"SessAliveSec\":%d}",
                 config.InitString, config.MaxNumSess, config.SessAliveSec);
    }
    
    printf("Initializing PPCS API with JSON parameter...\n");
    printf("JSON: %s\n\n", json_init_string);
    
    // 使用 JSON 字符串初始化（新 API 格式）
    ret = PPCS_Initialize(json_init_string);
    if (ret != ERROR_PPCS_SUCCESSFUL && ret != ERROR_PPCS_ALREADY_INITIALIZED) {
        print_error("PPCS_Initialize", ret);
        printf("[INFO] If JSON initialization failed, the API may use plain InitString format\n");
        printf("[INFO] Trying with plain InitString...\n\n");
        ret = PPCS_Initialize((char *)config.InitString);
        if (ret != ERROR_PPCS_SUCCESSFUL && ret != ERROR_PPCS_ALREADY_INITIALIZED) {
            print_error("PPCS_Initialize (fallback)", ret);
            return -1;
        }
    }
    printf("PPCS initialized successfully\n\n");
    
    // 网络检测
    printf("Detecting network...\n");
    ret = PPCS_NetworkDetect(&net_info, config.UDPPort);
    if (ret < 0) {
        print_error("PPCS_NetworkDetect", ret);
    } else {
        print_network_info(&net_info);
    }
    
    // 命令行参数检查 - 可以覆盖配置文件中的 DID
    const char *target_did = config.TargetDID;
    if (argc > 1) {
        target_did = argv[1];
        printf("[INFO] Using DID from command line: %s\n\n", target_did);
    } else if (strlen(config.TargetDID) == 0) {
        printf("Usage:\n");
        printf("  %s [TARGET_DID]     - Connect to a device (optional, can use config file)\n", argv[0]);
        printf("\nExample:\n");
        printf("  %s ABC123DEFG456HIJ789\n", argv[0]);
        printf("\nNote:\n");
        printf("  You can configure TargetDID in %s to avoid command line input\n", CONFIG_FILE);
        PPCS_DeInitialize();
        return 0;
    }
    
    // 连接到目标设备
    INT32 session_handle = connect_to_device(target_did, &config);
    if (session_handle < 0) {
        printf("[ERROR] Failed to connect to device\n");
        PPCS_DeInitialize();
        return -1;
    }
    
    // 初始化网络线程队列与线程
    InitializeCriticalSection(&g_pkg_cs);
    g_pkg_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    DWORD tid = 0;
    g_net_thread = CreateThread(NULL, 0, network_reader_thread, (LPVOID)(intptr_t)session_handle, 0, &tid);
    if (!g_net_thread) {
        printf("[WARN] Failed to create network reader thread\n");
    } else {
        printf("[Info] Network reader thread started (tid=%u)\n", (unsigned)tid);
    }
    
    Sleep(1000);
    // ====== 创建控制面板和数据处理 ======
    printf("================================================\n");
    printf("Starting P2P Client with Control Panel...\n");
    printf("================================================\n\n");
    
    // 创建视频流管理器
    VideoStreamManager* video_mgr = create_video_stream_manager("output_video");
    if (!video_mgr) {
        printf("[ERROR] Failed to create video stream manager\n");
        PPCS_Close(session_handle);
        PPCS_DeInitialize();
        return -1;
    }
    
    // 创建应用程序上下文
    AppContext app_ctx = {0};
    app_ctx.session_handle = session_handle;
    app_ctx.video_mgr = video_mgr;
    app_ctx.live_started = 0;
    app_ctx.playback_started = 0;
    
    // 创建三按钮控制面板（直播 / 回放 / 录像列表）
    ControlPanel* panel = control_panel_create_with_record_list(
        "P2P 视频控制面板",
        on_live_button_clicked,
        on_playback_button_clicked,
        on_record_list_button_clicked,
        &app_ctx
    );
    
    if (!panel) {
        printf("[ERROR] Failed to create control panel\n");
        destroy_video_stream_manager(video_mgr);
        PPCS_Close(session_handle);
        PPCS_DeInitialize();
        return -1;
    }
    
    printf("[App] Control panel created\n");
    printf("[App] Click buttons to start live stream or playback\n");
    printf("[App] Close the control panel window to exit\n\n");
    
    // 主循环：处理控制面板事件 + 接收视频数据
    // 使用简化的接收循环（后续可以改成线程）
    time_t start_time = time(NULL);
    
    while (1) {
        // 检查运行时间（最多运行 10 分钟）
        if (time(NULL) - start_time > 600) {
            printf("\n[App] Maximum runtime reached (10 minutes)\n");
            break;
        }
        
        // 处理控制面板事件
        if (!control_panel_poll_events(panel)) {
            printf("\n[App] Control panel closed by user\n");
            break;
        }
        
        // 处理所有视频窗口的事件
        for (int i = 0; i < 5; i++) {
            if (video_mgr->streams[i] && video_mgr->streams[i]->display) {
                video_display_poll_events(video_mgr->streams[i]->display);
            }
        }
        
        // 从网络队列消费已组装的完整包（最多处理 N 个以避免长时间占用主循环）
        int processed = 0;
        const int MAX_PROC_PER_LOOP = 8;
        while (processed < MAX_PROC_PER_LOOP) {
            PackageNode* node = pop_package_from_queue();
            if (!node) break;

            unsigned char* pkg = node->data;
            int pkg_len = node->len;

            int is_json = (memcmp(pkg, PKG_JSON_PREFIX, 4) == 0);
            if (is_json) {
                handle_command_package(pkg, pkg_len);
            } else {
                handle_video_package(video_mgr, pkg, pkg_len);
            }

            free(pkg);
            free(node);
            processed++;
        }
        
        // 短暂休眠
        Sleep(1);
    }
    
    // 清理控制面板
    printf("\n[Cleanup] Destroying control panel...\n");
    control_panel_destroy(panel);
    
    // 清理视频管理器
    printf("\n[Cleanup] Closing video streams...\n");
    destroy_video_stream_manager(video_mgr);
    
    // 关闭连接
    printf("\n[Cleanup] Closing connection...\n");
    ret = PPCS_Close(session_handle);
    if (ret < 0) {
        print_error("PPCS_Close", ret);
    } else {
        printf("Session closed successfully\n\n");
    }
    
    // 停止网络线程并清理队列
    if (g_net_thread) {
        g_net_thread_run = 0; // 请求线程退出
        // 唤醒网络线程（如果在阻塞读之外等待）
        if (g_pkg_event) SetEvent(g_pkg_event);
        WaitForSingleObject(g_net_thread, 2000);
        CloseHandle(g_net_thread);
        g_net_thread = NULL;
    }

    // 清空队列残余包
    while (1) {
        PackageNode* n = pop_package_from_queue();
        if (!n) break;
        if (n->data) free(n->data);
        free(n);
    }
    if (g_pkg_event) { CloseHandle(g_pkg_event); g_pkg_event = NULL; }
    DeleteCriticalSection(&g_pkg_cs);
    
    // 反初始化 PPCS
    printf("Deinitializing PPCS API...\n");
    ret = PPCS_DeInitialize();
    if (ret < 0) {
        print_error("PPCS_DeInitialize", ret);
    } else {
        printf("PPCS deinitialized successfully\n");
    }
    
    printf("\n");
    printf("*****************************************************\n");
    printf("*                    Done\n");
    printf("*****************************************************\n\n");
    
    return 0;
}
