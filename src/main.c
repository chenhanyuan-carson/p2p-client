#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <time.h>
#include "ppcs_core.h"
#include "video_manager.h"
#include "image_handler.h"
#include "command_handler.h"
#include "app_context.h"
#include "control_panel.h"
#include "control_panel_tab.h"
#include "cJSON.h"

int main(int argc, char* argv[]) {
    INT32 ret;
    st_PPCS_NetInfo net_info;
    Config config;

    print_api_info();
    init_config(&config);
    if (!validate_config(&config)) return -1;
    print_config(&config);
    init_record_list();

    char json_init_string[2048];
    if (strlen(config.APILogFile) > 0) snprintf(json_init_string, sizeof(json_init_string), "{\"InitString\":\"%s\",\"MaxNumSess\":%d,\"SessAliveSec\":%d,\"APILogFile\":\"%s\"}", config.InitString, config.MaxNumSess, config.SessAliveSec, config.APILogFile);
    else snprintf(json_init_string, sizeof(json_init_string), "{\"InitString\":\"%s\",\"MaxNumSess\":%d,\"SessAliveSec\":%d}", config.InitString, config.MaxNumSess, config.SessAliveSec);

    ret = PPCS_Initialize(json_init_string);
    if (ret != ERROR_PPCS_SUCCESSFUL && ret != ERROR_PPCS_ALREADY_INITIALIZED) {
        ret = PPCS_Initialize((char*)config.InitString);
        if (ret != ERROR_PPCS_SUCCESSFUL && ret != ERROR_PPCS_ALREADY_INITIALIZED) { print_error("PPCS_Initialize (fallback)", ret); return -1; }
    }

    ret = PPCS_NetworkDetect(&net_info, config.UDPPort);
    if (ret < 0) print_error("PPCS_NetworkDetect", ret);

    const char* target_did = config.TargetDID;
    if (argc > 1) target_did = argv[1];
    if (strlen(target_did) == 0) { printf("Usage: %s [TARGET_DID]\n", argv[0]); PPCS_DeInitialize(); return 0; }

    INT32 session_handle = connect_to_device(target_did, &config);
    if (session_handle < 0) { PPCS_DeInitialize(); return -1; }

    if (ppcs_start_network(session_handle) != 0) { printf("[WARN] Failed to start network thread\n"); }

    VideoStreamManager* video_mgr = create_video_stream_manager("output_video");
    AppContext app_ctx = {0}; app_ctx.session_handle = session_handle; app_ctx.video_mgr = video_mgr; app_ctx.live_started = 0; app_ctx.playback_started = 0;

    ControlPanel* panel = control_panel_create_tabbed("P2P Client", on_command_triggered, &app_ctx);
    if (!panel) { destroy_video_stream_manager(video_mgr); PPCS_Close(session_handle); PPCS_DeInitialize(); return -1; }

    time_t start_time = time(NULL);
    while (1) {
        if (time(NULL) - start_time > 600) {
            printf("[Debug] Exiting main loop: timeout reached\n");
            break;
        }
        if (!control_panel_poll_events(panel)) {
            printf("[Debug] Exiting main loop: control_panel_poll_events failed\n");
            break;
        }
        if (!video_manager_poll_events(video_mgr)) {
            printf("[Debug] Exiting main loop: video_manager_poll_events failed\n");
            break;
        }
        int processed = 0; const int MAX_PROC_PER_LOOP = 8;
        while (processed < MAX_PROC_PER_LOOP) {
            PackageNode* node = ppcs_pop_package();
            if (!node) break;
            unsigned char* pkg = node->data; int pkg_len = node->len;
            int is_json = (memcmp(pkg, "#nsj", 4) == 0);
            int is_image = (memcmp(pkg, "$gmi", 4) == 0);
            int is_video = (memcmp(pkg, "$div", 4) == 0);
            if (is_json) handle_command_package(pkg, pkg_len);
            else if (is_image) handle_image_package(pkg, pkg_len);
            else if (is_video) handle_video_package(video_mgr, pkg, pkg_len);
            free(pkg); free(node); processed++; }
        Sleep(1);
    }

    printf("[Debug] Program exiting: cleaning up resources\n");
    printf("[Debug] Destroying control panel\n");
    control_panel_destroy(panel);
    destroy_video_stream_manager(video_mgr);
    PPCS_Close(session_handle);
    ppcs_stop_network();
    destroy_record_list();
    PPCS_DeInitialize();
    return 0;
}
