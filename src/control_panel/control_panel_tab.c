#include "control_panel_tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>

// 控制 ID
#define IDC_TAB 1000
#define IDC_LIVE_START 1001
#define IDC_LIVE_STOP 1002

// Record Tab IDs
#define IDC_RECORDS_LIST 1021
#define IDC_RECORDS_PLAYBACK 1022
#define IDC_RECORD_STOP 1028

// TC Tab IDs (Timelapse & Capture)
#define IDC_SNAPSHOT_IMG 1023
#define IDC_TIMELAPSE_START 1024
#define IDC_TIMELAPSE_STOP 1025
#define IDC_TIMELAPSE_LIST 1026
#define IDC_TIMELAPSE_DOWNLOAD 1027

// Settings Tab IDs
#define IDC_SETTINGS_RESTART 1031
#define IDC_SETTINGS_RESET 1032
#define IDC_SETTINGS_OTA 1033
#define IDC_GET_DEVICE_CONFIG 1034
#define IDC_GET_SDCARD_INFO 1035
#define IDC_SDCARD_FORMAT 1036
#define IDC_SDCARD_POP 1037

#define IDC_STATUS_LABEL 2000

// 每个选项卡的控件
typedef struct {
    HWND tab_page;           // 选项卡页面容器
    HWND status_label;       // 状态标签
    HWND* buttons;           // 按钮数组
    int button_count;        // 按钮数量
} TabPage;

// 控制面板结构
struct ControlPanel {
    HWND hwnd;
    HWND tab_ctrl;           // 选项卡控件
    TabPage tabs[TAB_COUNT]; // 每个选项卡
    
    CommandCallback command_callback;
    void* user_data;
    
    int running;
    int current_tab;
};

// 全局变量
static ControlPanel* g_panel = NULL;

// 窗口过程
LRESULT CALLBACK ControlPanelTabWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NOTIFY: {
            LPNMHDR pnmhdr = (LPNMHDR)lParam;
            if (pnmhdr->idFrom == IDC_TAB && pnmhdr->code == TCN_SELCHANGE) {
                if (g_panel) {
                    int old_tab = g_panel->current_tab;
                    int new_tab = TabCtrl_GetCurSel(g_panel->tab_ctrl);
                    
                    const wchar_t* tab_full_names[] = {L"Live Stream", L"Record", L"TC", L"Settings"};
                    printf("[ControlPanel] Tab changed to: %S\n", tab_full_names[new_tab]);
                    
                    // Hide buttons on old tab
                    if (old_tab >= 0 && old_tab < TAB_COUNT && g_panel->tabs[old_tab].buttons) {
                        for (int i = 0; i < g_panel->tabs[old_tab].button_count; i++) {
                            ShowWindow(g_panel->tabs[old_tab].buttons[i], SW_HIDE);
                        }
                        if (g_panel->tabs[old_tab].status_label) {
                            ShowWindow(g_panel->tabs[old_tab].status_label, SW_HIDE);
                        }
                    }
                    
                    // Show buttons on new tab
                    if (new_tab >= 0 && new_tab < TAB_COUNT && g_panel->tabs[new_tab].buttons) {
                        for (int i = 0; i < g_panel->tabs[new_tab].button_count; i++) {
                            ShowWindow(g_panel->tabs[new_tab].buttons[i], SW_SHOW);
                        }
                        if (g_panel->tabs[new_tab].status_label) {
                            ShowWindow(g_panel->tabs[new_tab].status_label, SW_SHOW);
                        }
                    }
                    
                    g_panel->current_tab = new_tab;
                }
            }
            break;
        }
        
        case WM_COMMAND: {
            if (g_panel && g_panel->command_callback) {
                int control_id = LOWORD(wParam);
                int notification = HIWORD(wParam);
                
                if (notification == BN_CLICKED) {
                    CommandID cmd_id = CMD_LIVE_START;
                    
                    switch (control_id) {
                        case IDC_LIVE_START:
                            cmd_id = CMD_LIVE_START;
                            break;
                        case IDC_LIVE_STOP:
                            cmd_id = CMD_LIVE_STOP;
                            break;
                        case IDC_RECORDS_LIST:
                            cmd_id = CMD_GET_RECORDS_LIST;
                            break;
                        case IDC_RECORDS_PLAYBACK:
                            cmd_id = CMD_PLAYBACK_RECORDS;
                            break;
                        case IDC_RECORD_STOP:
                            cmd_id = CMD_RECORD_STOP;
                            break;
                        case IDC_SNAPSHOT_IMG:
                            cmd_id = CMD_SNAPSHOT_IMG;
                            break;
                        case IDC_TIMELAPSE_START:
                            cmd_id = CMD_TIMELAPSE_START;
                            break;
                        case IDC_TIMELAPSE_STOP:
                            cmd_id = CMD_TIMELAPSE_STOP;
                            break;
                        case IDC_TIMELAPSE_LIST:
                            cmd_id = CMD_GET_TIMELAPSE_LIST;
                            break;
                        case IDC_TIMELAPSE_DOWNLOAD:
                            cmd_id = CMD_TIMELAPSE_DOWNLOAD;
                            break;
                        case IDC_SETTINGS_RESTART:
                            cmd_id = CMD_SETTINGS_RESTART;
                            break;
                        case IDC_SETTINGS_RESET:
                            cmd_id = CMD_SETTINGS_RESET;
                            break;
                        case IDC_SETTINGS_OTA:
                            cmd_id = CMD_OTA_UPGRADE;
                            break;
                        case IDC_GET_DEVICE_CONFIG:
                            cmd_id = CMD_GET_DEVICE_CONFIG;
                            break;
                        case IDC_GET_SDCARD_INFO:
                            cmd_id = CMD_GET_SDCARD_INFO;
                            break;
                        case IDC_SDCARD_FORMAT:
                            cmd_id = CMD_SDCARD_FORMAT;
                            break;
                        case IDC_SDCARD_POP:
                            cmd_id = CMD_SDCARD_POP;
                            break;
                        default:
                            return DefWindowProc(hwnd, msg, wParam, lParam);
                    }
                    
                    printf("[ControlPanel] Command triggered: 0x%X\n", cmd_id);
                    g_panel->command_callback(cmd_id, g_panel->user_data);
                }
            }
            break;
        }
        
        case WM_CLOSE:
            printf("[Debug] WM_CLOSE triggered\n");
            if (g_panel) {
                printf("[Debug] g_panel->running set to 0\n");
                g_panel->running = 0;
            }
            printf("[Debug] DestroyWindow called in control_panel_tab for hwnd: %p\n", hwnd);
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            printf("[Debug] WM_DESTROY received in control_panel_tab for hwnd: %p\n", hwnd);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 创建单个选项卡页面
static void create_tab_page(ControlPanel* panel, int tab_id) {
    TabPage* tab = &panel->tabs[tab_id];
    HWND parent = panel->hwnd;
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Microsoft YaHei");
    
    // 状态标签
    tab->status_label = CreateWindowW(
        L"STATIC",
        L"Status: Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        30, 50, 350, 25,
        parent, (HMENU)(INT_PTR)(IDC_STATUS_LABEL + tab_id),
        GetModuleHandle(NULL), NULL
    );
    SendMessage(tab->status_label, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // 根据选项卡类型创建不同的按钮
    int y_pos = 85;
    
    if (tab_id == TAB_LIVE) {
        // Live Tab - 简洁布局 (2 个按钮)
        HWND btn_start = CreateWindowW(
            L"BUTTON", L"Start Live",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            50, y_pos, 120, 35,
            parent, (HMENU)IDC_LIVE_START,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_start, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        HWND btn_stop = CreateWindowW(
            L"BUTTON", L"Stop Live",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            190, y_pos, 120, 35,
            parent, (HMENU)IDC_LIVE_STOP,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_stop, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        tab->button_count = 2;
        tab->buttons = (HWND*)malloc(sizeof(HWND) * 2);
        tab->buttons[0] = btn_start;
        tab->buttons[1] = btn_stop;
    }
    else if (tab_id == TAB_RECORD) {
        // Record Tab - 录像管理 (3 个按钮)
        HWND btn_records_list = CreateWindowW(
            L"BUTTON", L"Query Records",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            25, y_pos, 120, 32,
            parent, (HMENU)IDC_RECORDS_LIST,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_records_list, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        HWND btn_records_play = CreateWindowW(
            L"BUTTON", L"Playback",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            160, y_pos, 120, 32,
            parent, (HMENU)IDC_RECORDS_PLAYBACK,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_records_play, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        HWND btn_record_stop = CreateWindowW(
            L"BUTTON", L"Stop Record",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            295, y_pos, 120, 32,
            parent, (HMENU)IDC_RECORD_STOP,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_record_stop, WM_SETFONT, (WPARAM)hFont, TRUE);

        tab->button_count = 3;
        tab->buttons = (HWND*)malloc(sizeof(HWND) * 3);
        tab->buttons[0] = btn_records_list;
        tab->buttons[1] = btn_records_play;
        tab->buttons[2] = btn_record_stop;
    }
    else if (tab_id == TAB_TC) {
        // TC Tab - 延时摄影和抓拍 (5 个按钮)
        // 第一行: 延时摄影
        HWND btn_timelapse_start = CreateWindowW(
            L"BUTTON", L"TL Start",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            25, y_pos, 80, 32,
            parent, (HMENU)IDC_TIMELAPSE_START,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_timelapse_start, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND btn_timelapse_stop = CreateWindowW(
            L"BUTTON", L"TL Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, y_pos, 80, 32,
            parent, (HMENU)IDC_TIMELAPSE_STOP,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_timelapse_stop, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND btn_timelapse_list = CreateWindowW(
            L"BUTTON", L"TL List",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            215, y_pos, 80, 32,
            parent, (HMENU)IDC_TIMELAPSE_LIST,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_timelapse_list, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND btn_timelapse_download = CreateWindowW(
            L"BUTTON", L"TL Download",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            310, y_pos, 100, 32,
            parent, (HMENU)IDC_TIMELAPSE_DOWNLOAD,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_timelapse_download, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 第二行: 抓拍
        HWND btn_snapshot = CreateWindowW(
            L"BUTTON", L"Snapshot",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            25, y_pos + 45, 120, 32,
            parent, (HMENU)IDC_SNAPSHOT_IMG,
            GetModuleHandle(NULL), NULL
        );
        SendMessage(btn_snapshot, WM_SETFONT, (WPARAM)hFont, TRUE);

        tab->button_count = 5;
        tab->buttons = (HWND*)malloc(sizeof(HWND) * 5);
        tab->buttons[0] = btn_timelapse_start;
        tab->buttons[1] = btn_timelapse_stop;
        tab->buttons[2] = btn_timelapse_list;
        tab->buttons[3] = btn_timelapse_download;
        tab->buttons[4] = btn_snapshot;
    }
    else if (tab_id == TAB_SETTINGS) {
        // Settings Tab - 网格布局 (7 个按钮)
        int grid_spacing_x = 8;
        int grid_spacing_y = 8;
        int button_width = 105;
        int button_height = 32;
        int start_x = 25;
        int start_y = 85;

        HWND buttons[] = {
            CreateWindowW(L"BUTTON", L"Restart", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x, start_y, button_width, button_height, parent, (HMENU)IDC_SETTINGS_RESTART, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x + (button_width + grid_spacing_x), start_y, button_width, button_height, parent, (HMENU)IDC_SETTINGS_RESET, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"OTA Upgrade", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x + 2 * (button_width + grid_spacing_x), start_y, button_width, button_height, parent, (HMENU)IDC_SETTINGS_OTA, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"Device Info", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x, start_y + (button_height + grid_spacing_y), button_width, button_height, parent, (HMENU)IDC_GET_DEVICE_CONFIG, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"SD Card Info", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x + (button_width + grid_spacing_x), start_y + (button_height + grid_spacing_y), button_width, button_height, parent, (HMENU)IDC_GET_SDCARD_INFO, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"Format SD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x + 2 * (button_width + grid_spacing_x), start_y + (button_height + grid_spacing_y), button_width, button_height, parent, (HMENU)IDC_SDCARD_FORMAT, GetModuleHandle(NULL), NULL),
            CreateWindowW(L"BUTTON", L"Eject SD", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          start_x, start_y + 2 * (button_height + grid_spacing_y), button_width, button_height, parent, (HMENU)IDC_SDCARD_POP, GetModuleHandle(NULL), NULL)
        };

        for (int i = 0; i < 7; i++) {
            if (!buttons[i]) {
                printf("[UI] ERROR: Failed to create button %d\n", i);
            } else {
                SendMessage(buttons[i], WM_SETFONT, (WPARAM)hFont, TRUE);
            }
        }

        tab->button_count = 7;
        tab->buttons = (HWND*)malloc(sizeof(HWND) * 7);
        memcpy(tab->buttons, buttons, sizeof(buttons));
    }
}

/**
 * 创建选项卡式控制面板
 */
ControlPanel* control_panel_create_tabbed(const char* title,
                                          CommandCallback command_callback,
                                          void* user_data) {
    ControlPanel* panel = (ControlPanel*)malloc(sizeof(ControlPanel));
    if (!panel) {
        printf("[ControlPanel] Failed to allocate memory\n");
        return NULL;
    }
    
    memset(panel, 0, sizeof(ControlPanel));
    panel->command_callback = command_callback;
    panel->user_data = user_data;
    panel->running = 1;
    panel->current_tab = 0;
    
    g_panel = panel;
    
    // 注册窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = ControlPanelTabWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "P2PControlPanelTabClass";
    
    if (!RegisterClassEx(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[ControlPanel] Failed to register window class\n");
            free(panel);
            return NULL;
        }
    }
    
    // 创建窗口
    wchar_t title_w[256] = {L"video"};
    MultiByteToWideChar(CP_ACP, 0, title, -1, title_w, 256);
    
    panel->hwnd = CreateWindowExW(
        0,
        L"P2PControlPanelTabClass",
        title_w,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 300,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!panel->hwnd) {
        printf("[ControlPanel] Failed to create window\n");
        free(panel);
        return NULL;
    }
    
    // 创建选项卡控件
    InitCommonControls();
    panel->tab_ctrl = CreateWindowW(
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_FIXEDWIDTH,
        10, 10, 450, 260,
        panel->hwnd,
        (HMENU)IDC_TAB,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!panel->tab_ctrl) {
        printf("[ControlPanel] Failed to create tab control\n");
        DestroyWindow(panel->hwnd);
        free(panel);
        return NULL;
    }
    
    // 添加选项卡
    TCITEMW tie;
    memset(&tie, 0, sizeof(tie));
    tie.mask = TCIF_TEXT;
    
    const wchar_t* tab_names[] = {L"Live", L"Record", L"TC", L"Settings"};
    for (int i = 0; i < TAB_COUNT; i++) {
        tie.pszText = (wchar_t*)tab_names[i];
        tie.cchTextMax = wcslen(tab_names[i]) + 1;
        printf("[Tab] Adding tab %d: '%S' (len=%zu)\n", i, tab_names[i], wcslen(tab_names[i]));
        int result = TabCtrl_InsertItem(panel->tab_ctrl, i, &tie);
        printf("[Tab] InsertItem result: %d\n", result);
        create_tab_page(panel, i);
    }
    
    // Set tab item size after all items are inserted
    printf("[Tab] Before SetItemSize\n");
    LRESULT res = TabCtrl_SetItemSize(panel->tab_ctrl, 100, 30);
    printf("[Tab] SetItemSize result: %lld\n", (long long)res);
    printf("[Tab] After SetItemSize\n");
    
    // Hide buttons on all tabs except the first one
    for (int i = 1; i < TAB_COUNT; i++) {
        if (panel->tabs[i].buttons) {
            for (int j = 0; j < panel->tabs[i].button_count; j++) {
                ShowWindow(panel->tabs[i].buttons[j], SW_HIDE);
            }
        }
        if (panel->tabs[i].status_label) {
            ShowWindow(panel->tabs[i].status_label, SW_HIDE);
        }
    }
    
    // 显示窗口
    ShowWindow(panel->hwnd, SW_SHOW);
    UpdateWindow(panel->hwnd);
    
    printf("[ControlPanel] Tabbed control panel created\n");
    
    return panel;
}

/**
 * 更新选项卡状态
 */
void control_panel_update_status(ControlPanel* panel, int tab_id, const wchar_t* status_text) {
    if (!panel || tab_id < 0 || tab_id >= TAB_COUNT) return;

    if (panel->tabs[tab_id].status_label) {
        SetWindowTextW(panel->tabs[tab_id].status_label, status_text);
    }
}
