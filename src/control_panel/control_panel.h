#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include <windows.h>

// 控制面板句柄
typedef struct ControlPanel ControlPanel;

// 按钮回调函数类型
typedef void (*ButtonCallback)(void* user_data);

// 三按钮面板（直播、回放、录像列表）创建函数声明
ControlPanel* control_panel_create_with_record_list(const char* title,
                                   ButtonCallback live_callback,
                                   ButtonCallback playback_callback,
                                   ButtonCallback record_list_callback,
                                   void* user_data);

/**
 * 创建控制面板
 * @param title 窗口标题
 * @param live_callback 直播按钮回调
 * @param playback_callback 录像回放按钮回调
 * @param user_data 用户数据指针
 * @return 控制面板句柄
 */
ControlPanel* control_panel_create(const char* title,
                                   ButtonCallback live_callback,
                                   ButtonCallback playback_callback,
                                   void* user_data);

/**
 * 处理控制面板事件（非阻塞）
 * @param panel 控制面板句柄
 * @return 1=继续运行, 0=窗口已关闭
 */
int control_panel_poll_events(ControlPanel* panel);

/**
 * 更新按钮状态
 * @param panel 控制面板句柄
 * @param live_enabled 直播按钮是否可用
 * @param playback_enabled 录像按钮是否可用
 */
void control_panel_update_buttons(ControlPanel* panel, int live_enabled, int playback_enabled);

/**
 * 销毁控制面板
 * @param panel 控制面板句柄
 */
void control_panel_destroy(ControlPanel* panel);

#endif // CONTROL_PANEL_H
