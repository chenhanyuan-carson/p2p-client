#include "control_panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BTN_LIVE 1001
#define BTN_PLAYBACK 1002
#define BTN_RECORD_LIST 1003

// 控制面板结构
struct ControlPanel {
    HWND hwnd;
    HWND btn_live;
    HWND btn_playback;
    HWND btn_record_list;
    HWND status_label;
    
    ButtonCallback live_callback;
    ButtonCallback playback_callback;
    ButtonCallback record_list_callback;
    void* user_data;
    
    int running;
};

// 全局变量用于窗口过程访问
static ControlPanel* g_panel = NULL;

// 窗口过程
LRESULT CALLBACK ControlPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (g_panel) {
                int button_id = LOWORD(wParam);
                
                if (button_id == BTN_LIVE && g_panel->live_callback) {
                    printf("[ControlPanel] Live button clicked\n");
                    SetWindowTextW(g_panel->status_label, L"状态: 正在启动直播...");
                    g_panel->live_callback(g_panel->user_data);
                    SetWindowTextW(g_panel->status_label, L"状态: 直播已启动");
                }
                else if (button_id == BTN_PLAYBACK && g_panel->playback_callback) {
                    printf("[ControlPanel] Playback button clicked\n");
                    SetWindowTextW(g_panel->status_label, L"状态: 正在启动录像回放...");
                    g_panel->playback_callback(g_panel->user_data);
                    SetWindowTextW(g_panel->status_label, L"状态: 录像回放已启动");
                }
                else if (button_id == BTN_RECORD_LIST && g_panel->record_list_callback) {
                    printf("[ControlPanel] RecordList button clicked\n");
                    SetWindowTextW(g_panel->status_label, L"状态: 查询录像列表...");
                    g_panel->record_list_callback(g_panel->user_data);
                    SetWindowTextW(g_panel->status_label, L"状态: 录像列表已查询");
                }
            }
            break;
            
        case WM_CLOSE:
            printf("[Debug] WM_CLOSE received in control_panel for hwnd: %p\n", hwnd);
            if (g_panel) {
                printf("[Debug] g_panel->running set to 0\n");
                g_panel->running = 0;
            }
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            printf("[Debug] WM_DESTROY received in control_panel for hwnd: %p\n", hwnd);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/**
 * 创建控制面板
 */
ControlPanel* control_panel_create(const char* title,
                                   ButtonCallback live_callback,
                                   ButtonCallback playback_callback,
                                   void* user_data) {
    ControlPanel* panel = (ControlPanel*)malloc(sizeof(ControlPanel));
    if (!panel) {
        printf("[ControlPanel] Failed to allocate memory\n");
        return NULL;
    }
    
    memset(panel, 0, sizeof(ControlPanel));
    panel->live_callback = live_callback;
    panel->playback_callback = playback_callback;
    panel->user_data = user_data;
    panel->running = 1;
    
    g_panel = panel;
    
    // 注册窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = ControlPanelWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "P2PControlPanelClass";
    
    if (!RegisterClassEx(&wc)) {
        // 可能已经注册过了
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[ControlPanel] Failed to register window class\n");
            free(panel);
            return NULL;
        }
    }
    
    // 创建窗口（使用 Unicode 版本）
    panel->hwnd = CreateWindowExW(
        0,
        L"P2PControlPanelClass",
        L"P2P 视频控制面板",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        400, 200,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!panel->hwnd) {
        printf("[ControlPanel] Failed to create window\n");
        free(panel);
        return NULL;
    }
    
    // 设置中文字体
    HFONT hFont = CreateFontW(
        18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei"
    );
    
    // 创建状态标签（使用 Unicode）
    panel->status_label = CreateWindowW(
        L"STATIC",
        L"状态: 就绪",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 20, 360, 30,
        panel->hwnd,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    SendMessage(panel->status_label, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 创建直播按钮（使用 Unicode）
    panel->btn_live = CreateWindowW(
        L"BUTTON",
        L"开始直播",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        50, 70, 120, 40,
        panel->hwnd,
        (HMENU)BTN_LIVE,
        GetModuleHandle(NULL),
        NULL
    );
    
    SendMessage(panel->btn_live, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 创建录像回放按钮（使用 Unicode）
    panel->btn_playback = CreateWindowW(
        L"BUTTON",
        L"录像回放",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        230, 70, 120, 40,
        panel->hwnd,
        (HMENU)BTN_PLAYBACK,
        GetModuleHandle(NULL),
        NULL
    );
    
    SendMessage(panel->btn_playback, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 创建录像列表按钮
    panel->btn_record_list = CreateWindowW(
        L"BUTTON",
        L"录像列表",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        50, 120, 120, 32,
        panel->hwnd,
        (HMENU)BTN_RECORD_LIST,
        GetModuleHandle(NULL),
        NULL
    );
    SendMessage(panel->btn_record_list, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 显示窗口
    ShowWindow(panel->hwnd, SW_SHOW);
    UpdateWindow(panel->hwnd);
    
    printf("[ControlPanel] Control panel created\n");
    
    return panel;
}

// 创建三按钮面板（包装现有创建函数并设置 record_list_callback）
ControlPanel* control_panel_create_with_record_list(const char* title,
                                   ButtonCallback live_callback,
                                   ButtonCallback playback_callback,
                                   ButtonCallback record_list_callback,
                                   void* user_data) {
    ControlPanel* panel = control_panel_create(title, live_callback, playback_callback, user_data);
    if (!panel) return NULL;
    panel->record_list_callback = record_list_callback;
    return panel;
}

/**
 * 处理控制面板事件
 */
int control_panel_poll_events(ControlPanel* panel) {
    if (!panel || !panel->running) {
        return 0;
    }
    
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            printf("[Debug] WM_QUIT received in control_panel_poll_events\n");
            panel->running = 0;
            return 0;
        } else {
            //printf("[Debug] Message received: 0x%X\n", msg.message);
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return panel->running;
}

/**
 * 更新按钮状态
 */
void control_panel_update_buttons(ControlPanel* panel, int live_enabled, int playback_enabled) {
    if (!panel) return;
    
    EnableWindow(panel->btn_live, live_enabled);
    EnableWindow(panel->btn_playback, playback_enabled);
}

/**
 * 销毁控制面板
 */
void control_panel_destroy(ControlPanel* panel) {
    printf("[Debug] control_panel_destroy called\n");
    if (panel && panel->hwnd) {
        printf("[Debug] Destroying window with hwnd: %p\n", panel->hwnd);
    }
    if (!panel) return;
    
    if (panel->hwnd) {
        printf("[Debug] DestroyWindow called in control_panel for hwnd: %p\n", panel->hwnd);
        DestroyWindow(panel->hwnd);
    }
    
    g_panel = NULL;
    free(panel);
    
    printf("[ControlPanel] Control panel destroyed\n");
}
