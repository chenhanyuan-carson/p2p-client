// Video Display Implementation
#include "video_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// FFmpeg 用于 YUV 转 RGB
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

// 显示窗口结构
struct VideoDisplay {
    HWND hwnd;                      // 窗口句柄
    HDC hdc;                        // 设备上下文
    HDC memDC;                      // 内存 DC
    HBITMAP hBitmap;                // 位图句柄
    HBITMAP hOldBitmap;             // 旧位图
    
    int width;                      // 窗口宽度
    int height;                     // 窗口高度
    int initialized;                // 初始化标志
    int should_close;               // 关闭标志
    
    // YUV 转 RGB 转换器
    struct SwsContext* sws_ctx;
    uint8_t* rgb_buffer;            // RGB 缓冲区
    int rgb_linesize;               // RGB 行大小
};

// 全局窗口实例（用于窗口过程）
static VideoDisplay* g_display = NULL;

// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            if (g_display) {
                g_display->should_close = 1;
            }
            return 0;
            
        case WM_DESTROY:
            // Removed unnecessary debug information
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == 'Q') {
                if (g_display) {
                    g_display->should_close = 1;
                }
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            // 不在这里绘制，由 render 函数控制
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/**
 * 创建显示窗口
 */
VideoDisplay* video_display_create(const char* title, int width, int height) {
    VideoDisplay* display = (VideoDisplay*)malloc(sizeof(VideoDisplay));
    if (!display) {
        printf("[Display] Failed to allocate display\n");
        return NULL;
    }
    
    memset(display, 0, sizeof(VideoDisplay));
    display->width = width;
    display->height = height;
    g_display = display;
    
    printf("[Display] Creating window: %s (%dx%d)\n", title, width, height);
    
    // 注册窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "P2PVideoWindow";
    
    if (!RegisterClassEx(&wc)) {
        // 可能已经注册过了，忽略错误
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            printf("[Display] Failed to register window class: %ld\n", GetLastError());
            free(display);
            return NULL;
        }
    }
    
    // 计算窗口大小（包含边框）
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    
    // 创建窗口
    display->hwnd = CreateWindowEx(
        0,
        "P2PVideoWindow",
        title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!display->hwnd) {
        printf("[Display] Failed to create window: %ld\n", GetLastError());
        free(display);
        return NULL;
    }
    
    // 获取设备上下文
    display->hdc = GetDC(display->hwnd);
    if (!display->hdc) {
        printf("[Display] Failed to get DC\n");
        DestroyWindow(display->hwnd);
        free(display);
        return NULL;
    }
    
    // 创建内存 DC
    display->memDC = CreateCompatibleDC(display->hdc);
    if (!display->memDC) {
        printf("[Display] Failed to create memory DC\n");
        ReleaseDC(display->hwnd, display->hdc);
        DestroyWindow(display->hwnd);
        free(display);
        return NULL;
    }
    
    // 创建位图信息
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // 负值表示从上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;     // RGB24
    bmi.bmiHeader.biCompression = BI_RGB;
    
    // 创建 DIB
    void* bits = NULL;
    display->hBitmap = CreateDIBSection(display->memDC, &bmi, DIB_RGB_COLORS, 
                                       &bits, NULL, 0);
    if (!display->hBitmap) {
        printf("[Display] Failed to create bitmap\n");
        DeleteDC(display->memDC);
        ReleaseDC(display->hwnd, display->hdc);
        DestroyWindow(display->hwnd);
        free(display);
        return NULL;
    }
    
    display->hOldBitmap = (HBITMAP)SelectObject(display->memDC, display->hBitmap);
    
    // 分配 RGB 缓冲区
    display->rgb_linesize = width * 3;
    display->rgb_buffer = (uint8_t*)malloc(display->rgb_linesize * height);
    if (!display->rgb_buffer) {
        printf("[Display] Failed to allocate RGB buffer\n");
        SelectObject(display->memDC, display->hOldBitmap);
        DeleteObject(display->hBitmap);
        DeleteDC(display->memDC);
        ReleaseDC(display->hwnd, display->hdc);
        DestroyWindow(display->hwnd);
        free(display);
        return NULL;
    }
    
    display->initialized = 1;
    printf("[Display] Window created successfully\n");
    
    return display;
}

/**
 * 显示一帧 YUV 数据
 */
int video_display_render(VideoDisplay* display, VideoFrame* frame) {
    if (!display || !display->initialized || !frame) {
        return -1;
    }
    
    // 初始化 YUV 转 RGB 转换器（第一次）
    if (!display->sws_ctx) {
        display->sws_ctx = sws_getContext(
            frame->width, frame->height, AV_PIX_FMT_YUV420P,
            display->width, display->height, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, NULL, NULL, NULL
        );
        
        if (!display->sws_ctx) {
            printf("[Display] Failed to create sws context\n");
            return -1;
        }
    }
    
    // YUV 转 BGR24（Windows GDI 需要 BGR 顺序）
    uint8_t* dst_data[1] = { display->rgb_buffer };
    int dst_linesize[1] = { display->rgb_linesize };
    
    sws_scale(
        display->sws_ctx,
        (const uint8_t* const*)frame->data,
        frame->linesize,
        0,
        frame->height,
        dst_data,
        dst_linesize
    );
    
    // 将 RGB 数据复制到位图
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = display->width;
    bmi.bmiHeader.biHeight = -display->height;  // 负值：从上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    SetDIBits(display->memDC, display->hBitmap, 0, display->height,
             display->rgb_buffer, &bmi, DIB_RGB_COLORS);
    
    // 位块传输到窗口
    BitBlt(display->hdc, 0, 0, display->width, display->height,
           display->memDC, 0, 0, SRCCOPY);
    
    return 0;
}

/**
 * 处理窗口事件
 */
int video_display_poll_events(VideoDisplay* display) {
    if (!display || !display->initialized) {
        return 0;
    }
    
    if (display->should_close) {
        return 0;
    }
    
    MSG msg;
    while (PeekMessage(&msg, display->hwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 1;  // 窗口仍然打开
}

/**
 * 设置窗口标题
 */
void video_display_set_title(VideoDisplay* display, const char* title) {
    if (display && display->hwnd && title) {
        SetWindowText(display->hwnd, title);
    }
}

/**
 * 销毁显示窗口
 */
void video_display_destroy(VideoDisplay* display) {
    if (!display) return;
    
    printf("[Display] Destroying window...\n");
    
    if (display->sws_ctx) {
        sws_freeContext(display->sws_ctx);
    }
    
    if (display->rgb_buffer) {
        free(display->rgb_buffer);
    }
    
    if (display->hBitmap) {
        SelectObject(display->memDC, display->hOldBitmap);
        DeleteObject(display->hBitmap);
    }
    
    if (display->memDC) {
        DeleteDC(display->memDC);
    }
    
    if (display->hdc) {
        ReleaseDC(display->hwnd, display->hdc);
    }
    
    if (display->hwnd) {
        printf("[Debug] DestroyWindow called in video_display for hwnd: %p\n", display->hwnd);
        DestroyWindow(display->hwnd);
    }
    
    if (g_display == display) {
        g_display = NULL;
    }
    
    free(display);
    printf("[Display] Window destroyed\n");
}