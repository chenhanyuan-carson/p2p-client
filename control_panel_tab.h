#ifndef CONTROL_PANEL_TAB_H
#define CONTROL_PANEL_TAB_H

#include <windows.h>

// 控制面板句柄
typedef struct ControlPanel ControlPanel;

// 回调函数类型定义
typedef void (*CommandCallback)(int command_id, void* user_data);
typedef void (*TabChangedCallback)(int tab_id, void* user_data);

// 命令 ID 定义（用于区分不同的交互）
typedef enum {
    CMD_LIVE_START = 0x101,
    CMD_LIVE_STOP = 0x102,
    
    CMD_PLAYBACK_START = 0x201,
    CMD_PLAYBACK_STOP = 0x202,
    CMD_PLAYBACK_PAUSE = 0x203,
    CMD_PLAYBACK_RESUME = 0x204,
    
    CMD_RECORD_LIST_GET = 0x301,
    CMD_RECORD_PLAY = 0x302,
    
    CMD_SETTINGS_SAVE = 0x401,
    CMD_SETTINGS_RESTORE = 0x402,
    CMD_OTA_UPGRADE = 0x011,
} CommandID;

// 选项卡 ID
typedef enum {
    TAB_LIVE = 0,
    TAB_PLAYBACK = 1,
    TAB_RECORD = 2,
    TAB_SETTINGS = 3,
    TAB_COUNT = 4,
} TabID;

/**
 * 创建选项卡式控制面板
 * @param title 窗口标题
 * @param command_callback 命令回调（触发按钮/控件时调用）
 * @param user_data 用户数据指针
 * @return 控制面板句柄
 */
ControlPanel* control_panel_create_tabbed(const char* title,
                                          CommandCallback command_callback,
                                          void* user_data);

/**
 * 处理控制面板事件（非阻塞）
 * @param panel 控制面板句柄
 * @return 1=继续运行, 0=窗口已关闭
 */
int control_panel_poll_events(ControlPanel* panel);

/**
 * 更新选项卡内容（如状态标签）
 * @param panel 控制面板句柄
 * @param tab_id 选项卡 ID
 * @param status_text 要显示的状态文本
 */
void control_panel_update_status(ControlPanel* panel, int tab_id, const wchar_t* status_text);

/**
 * 启用/禁用按钮
 * @param panel 控制面板句柄
 * @param command_id 命令 ID
 * @param enabled 是否启用
 */
void control_panel_enable_command(ControlPanel* panel, int command_id, int enabled);

/**
 * 销毁控制面板
 * @param panel 控制面板句柄
 */
void control_panel_destroy(ControlPanel* panel);

#endif // CONTROL_PANEL_TAB_H
