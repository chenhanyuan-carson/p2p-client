#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include "video_manager.h"
#include "PPCS_API.h"

typedef struct {
    INT32 session_handle;
    VideoStreamManager* video_mgr;
    int live_started;
    int playback_started;
} AppContext;

#endif // APP_CONTEXT_H
