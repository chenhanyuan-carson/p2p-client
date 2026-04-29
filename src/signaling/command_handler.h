#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

typedef enum {
    JSON_CMD_HEARTBEAT               = 0x01,  // 心跳包
    JSON_CMD_SETTINGS_GET            = 0x02,  // 获取设备设置
    JSON_CMD_REBOOT                  = 0x03,  // 重启设备
    JSON_CMD_FACTORY_RESET           = 0x04,  // 恢复出厂设置
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
    JSON_CMD_DEVNAME_SET             = 0x56,  // 设备名称设置
    JSON_CMD_TEMP_DISPLAY_SET        = 0x57,  // 温度显示设置
    JSON_CMD_PAUSE_CAMERA_SET        = 0x58,  // 暂停摄像头设置
    JSON_CMD_SHOW_TIMESTAMP_SET      = 0x59,  // 时间戳显示设置

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
    JSON_CMD_REMOTE_MOTION_GET       = 0x116, // 远程运动检测参数获取
    JSON_CMD_REMOTE_MOTION_SET       = 0x117, // 远程运动检测参数设置

    JSON_CMD_PLAYBACK_START          = 0x201, // 开始回放
    JSON_CMD_PLAYBACK_STOP           = 0x202, // 停止回放
    JSON_CMD_PLAYBACK_CTRL           = 0x203, // 回放控制（播放/暂停/快进等）
    JSON_CMD_RECORD_END_PLAYBACK     = 0x204, // 获取录制设置
    JSON_CMD_RECORD_SETTINGS_GET     = 0x205, // 设置录制参数
    JSON_CMD_RECORD_SETTINGS_SET     = 0x206, // 获取有录像的日期
    JSON_CMD_RECORD_LIST_GET         = 0x207, // 获取指定月份有录制文件的日期列表

    JSON_CMD_TIMELAPSE_SET           = 0x301, // 设置延时摄影参数
    JSON_CMD_TIMELAPSE_LIST_GET      = 0x302, // 获取延时摄影片段列表
    JSON_CMD_TIMELAPSE_DOWNLOAD      = 0x303, // 下载延时摄影文件
    JSON_CMD_TIMELAPSE_DOWNLOAD_END  = 0x304, // 延时摄影结束下载
    JSON_CMD_TIMELAPSE_DELETE        = 0x305, // 删除延时摄影文件
	
    JSON_CMD_AUTOPHOTO_SET           = 0x311, // 设置自动拍照参数
    JSON_CMD_SNAPSHOT_IMG            = 0x312, // 抓拍图片(4K图片)

    JSON_CMD_SIDE_CAMERA_SET         = 0x321, // UVC子摄像头开关设置
    JSON_CMD_UVC_CONFIG_SET          = 0x322, // 设置UVC子摄像头配置
    JSON_CMD_UVC_HOTPLUG_NOTIFY      = 0x323, // UVC热插拔通知（设备→APP）
    JSON_CMD_UVC_PHOTO_GET           = 0x324, // 获取UVC子摄像头照片

    /* VPD 相关命令 (0x401-0x410) */
    JSON_CMD_VPD_DATA_GET            = 0x401, // 获取VPD传感器数据
    JSON_CMD_VPD_CONFIG_GET          = 0x402, // 获取VPD告警配置
    JSON_CMD_VPD_CONFIG_SET          = 0x403, // 设置VPD告警配置
    JSON_CMD_VPD_CALIBRATION_GET     = 0x404, // 获取VPD校准参数
    JSON_CMD_VPD_CALIBRATION_SET     = 0x405, // 设置VPD校准参数
    JSON_CMD_HISTORY_DATA_GET        = 0x406, // 获取历史数据记录
    JSON_CMD_VPD_ALERT_NOTIFY        = 0x410,  // VPD告警推送（设备→APP）

    JSON_CMD_TELNET_ENABLE_REQUEST   = 0x5001  // Telnet 临时开启请求（客户端→设备）
} ELD_CMD_CODE;

int handle_command_package(const unsigned char* package, int pkg_len);
int build_command_package(const char* json_data, unsigned char* package, int max_len, unsigned short pkg_id, unsigned short pkg_cmd);

// App callbacks exposed to control panel
void on_command_triggered(int command_id, void* user_data);
void on_telnet_enable_clicked(void* user_data);

// Record list utilities
void init_record_list(void);
void clear_record_list(void);
void destroy_record_list(void);
void add_record_item(const char* start_time, const char* end_time, int rec_type, long long start_ts, long long end_ts, unsigned int size, int frame_rate, int code_type);

#endif // COMMAND_HANDLER_H
