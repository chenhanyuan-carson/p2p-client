#include "command_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "ppcs_core.h"
#include "video_manager.h"
#include "control_panel_tab.h"
#include "control_panel.h"
#include "video_display.h"
#include "video_decoder.h"
#include "PPCS_API.h"
#include "app_context.h"
#include "protocol_defs.h"

#if 0
// Deprecated local JSON command defines kept for reference (use command_handler.h enum instead)
#endif

#define MAX_CMD_JSON_REASM (64 * 1024)
#define CMD_JSON_REASM_SLOTS 8

typedef struct {
    unsigned short pkg_id;
    size_t used;
    size_t cap;
    char* buf;
    int active;
} CmdJsonReasmSlot;

static CmdJsonReasmSlot g_cmd_json_slots[CMD_JSON_REASM_SLOTS] = {0};

static CmdJsonReasmSlot* get_cmd_json_slot(unsigned short pkg_id) {
    CmdJsonReasmSlot* empty = NULL;
    for (int i = 0; i < CMD_JSON_REASM_SLOTS; i++) {
        if (g_cmd_json_slots[i].active && g_cmd_json_slots[i].pkg_id == pkg_id) return &g_cmd_json_slots[i];
        if (!g_cmd_json_slots[i].active && empty == NULL) empty = &g_cmd_json_slots[i];
    }
    if (empty) {
        memset(empty, 0, sizeof(*empty));
        empty->pkg_id = pkg_id;
        empty->active = 1;
        return empty;
    }
    CmdJsonReasmSlot* slot = &g_cmd_json_slots[0];
    slot->pkg_id = pkg_id;
    slot->used = 0;
    slot->active = 1;
    return slot;
}

static void reset_cmd_json_slot(CmdJsonReasmSlot* slot) {
    if (!slot) return;
    slot->used = 0;
    slot->active = 0;
    slot->pkg_id = 0;
}

int cmd_json_reasm_append(unsigned short pkg_id,
                           unsigned short pkg_index,
                           const char* fragment,
                           int fragment_len,
                           const char** out_json,
                           int* out_len) {
    if (!fragment || fragment_len <= 0) return 0;
    CmdJsonReasmSlot* slot = get_cmd_json_slot(pkg_id);
    if (!slot) return 0;
    size_t needed = slot->used + (size_t)fragment_len + 1;
    if (needed > MAX_CMD_JSON_REASM) { reset_cmd_json_slot(slot); return 0; }
    if (slot->cap < needed) {
        size_t new_cap = slot->cap ? slot->cap : 4096;
        while (new_cap < needed) new_cap *= 2;
        if (new_cap > MAX_CMD_JSON_REASM) new_cap = MAX_CMD_JSON_REASM;
        char* new_buf = (char*)realloc(slot->buf, new_cap);
        if (!new_buf) { reset_cmd_json_slot(slot); return 0; }
        slot->buf = new_buf; slot->cap = new_cap;
    }
    memcpy(slot->buf + slot->used, fragment, (size_t)fragment_len);
    slot->used += (size_t)fragment_len;
    slot->buf[slot->used] = '\0';
    if (pkg_index != 0) return 0;
    if (out_json) *out_json = slot->buf;
    if (out_len) *out_len = (int)slot->used;
    slot->active = 0; slot->pkg_id = 0; slot->used = 0;
    return 1;
}

unsigned short calculate_checksum(const unsigned char* data, int len) {
    unsigned short checksum = 0;
    for (int i = 0; i < len; i++) checksum += data[i];
    return checksum;
}

int build_command_package(const char* json_data, unsigned char* package, int max_len, unsigned short pkg_id, unsigned short pkg_cmd) {
    // Use unified protocol definitions
    typedef PackageHeader_t TAG_PKG_HEADER_S;
    typedef PackageTail_t TAG_PKG_TAIL_S;

    const char* PKG_JSON_PREFIX = "#nsj";
    const unsigned short PKG_IDENT = 0x876e;
    int json_len = (int)strlen(json_data);
    int total_len = 4 + sizeof(TAG_PKG_HEADER_S) + json_len + sizeof(TAG_PKG_TAIL_S);
    if (total_len > max_len) return -1;
    int offset = 0;
    memcpy(package + offset, PKG_JSON_PREFIX, 4); offset += 4;
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset); memset(header,0,sizeof(TAG_PKG_HEADER_S));
    header->s16PkgIdent = PKG_IDENT; header->u16PkgLen = (unsigned short)json_len; header->u16PkgId = pkg_id; header->u16PkgIndex = 0; header->u16PkgKey = 0; header->u16PkgCmd = pkg_cmd; header->u8PkgSubHead = 0;
    offset += sizeof(TAG_PKG_HEADER_S);
    memcpy(package + offset, json_data, json_len); offset += json_len;
    TAG_PKG_TAIL_S* tail = (TAG_PKG_TAIL_S*)(package + offset); tail->u8Zero = 0; tail->u8Res = 0; tail->u16Check = calculate_checksum(package, offset);
    offset += sizeof(TAG_PKG_TAIL_S);
    return offset;
}

// Handle command package (parsing & record list handling)
int handle_command_package(const unsigned char* package, int pkg_len) {
    if (pkg_len < 4) return -1;
    const char* PKG_JSON_PREFIX = "#nsj";
    if (memcmp(package, PKG_JSON_PREFIX, 4) != 0) return -1;
    // Use unified protocol definitions
    typedef PackageHeader_t TAG_PKG_HEADER_S;
    int offset = 4;
    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(package + offset);
    offset += sizeof(TAG_PKG_HEADER_S);
    int json_len = header->u16PkgLen;
    if (json_len < 0 || offset + json_len > pkg_len) return -1;
    const char* assembled_json = NULL; int assembled_len = 0;
    int is_complete = cmd_json_reasm_append(header->u16PkgId, header->u16PkgIndex, (const char*)(package+offset), json_len, &assembled_json, &assembled_len);
    if (!is_complete) return 0;
    const char* json_response = assembled_json;
    printf("[Command] JSON response reassembled (length=%d):\n", assembled_len);
    printf("[Response] %s\n", json_response);
    // If record list response, parse
    if (header->u16PkgCmd == JSON_CMD_RECORD_LIST_GET || strstr(json_response, "JSON_CMD_RECORD_LIST_GET") != NULL) {
        cJSON *root = cJSON_Parse(json_response);
        if (!root) return -1;
        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (!data) { cJSON_Delete(root); return -1; }
        cJSON *recordlist = cJSON_GetObjectItemCaseSensitive(data, "recordList");
        if (!recordlist || !cJSON_IsArray(recordlist)) { cJSON_Delete(root); return -1; }
        cJSON *item = NULL; int idx = 0;
        cJSON_ArrayForEach(item, recordlist) {
            cJSON *start_time = cJSON_GetObjectItemCaseSensitive(item, "startTime"); if (!start_time) start_time = cJSON_GetObjectItemCaseSensitive(item, "start_time");
            cJSON *end_time = cJSON_GetObjectItemCaseSensitive(item, "endTime"); if (!end_time) end_time = cJSON_GetObjectItemCaseSensitive(item, "end_time");
            cJSON *rec_type = cJSON_GetObjectItemCaseSensitive(item, "recType"); if (!rec_type) rec_type = cJSON_GetObjectItemCaseSensitive(item, "record_type");
            cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "size");
            cJSON *frame_rate = cJSON_GetObjectItemCaseSensitive(item, "frameRate"); if (!frame_rate) frame_rate = cJSON_GetObjectItemCaseSensitive(item, "frame_rate");
            cJSON *code_type = cJSON_GetObjectItemCaseSensitive(item, "codeType"); if (!code_type) code_type = cJSON_GetObjectItemCaseSensitive(item, "code_type");
            if (!start_time || !end_time || !rec_type || !size || !frame_rate || !code_type) {
                printf("[RecordList][%d] Missing fields, skip\n", idx);
            } else {
                char start_str_buf[64] = {0}; char end_str_buf[64] = {0}; long long start_ts = 0; long long end_ts = 0;
                if (cJSON_IsNumber(start_time)) { start_ts = (long long)start_time->valuedouble; }
                else if (cJSON_IsString(start_time) && start_time->valuestring) { strncpy(start_str_buf, start_time->valuestring, sizeof(start_str_buf)-1); }
                if (cJSON_IsNumber(end_time)) { end_ts = (long long)end_time->valuedouble; }
                else if (cJSON_IsString(end_time) && end_time->valuestring) { strncpy(end_str_buf, end_time->valuestring, sizeof(end_str_buf)-1); }
                int rec_type_val = rec_type && cJSON_IsNumber(rec_type) ? rec_type->valueint : -1;
                unsigned int size_val = size && cJSON_IsNumber(size) ? (unsigned int)size->valuedouble : 0;
                int frame_rate_val = frame_rate && cJSON_IsNumber(frame_rate) ? frame_rate->valueint : -1;
                int code_type_val = code_type && cJSON_IsNumber(code_type) ? code_type->valueint : -1;
                printf("[RecordList][%d] startTime=%s(%lld), endTime=%s(%lld), size=%u, recType=%d, frameRate=%d, codeType=%d\n", idx, start_str_buf, start_ts, end_str_buf, end_ts, size_val, rec_type_val, frame_rate_val, code_type_val);
                // In this migration we don't centrally store record list here; caller can extend to save entries.
                idx++;
            }
        }
        cJSON_Delete(root);
        return 0;
    }
    if (header->u16PkgCmd == JSON_CMD_SETTINGS_GET) {
        cJSON* root = cJSON_Parse(json_response);
        if (!root) return -1;

        cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (data) {
            cJSON* devName = cJSON_GetObjectItemCaseSensitive(data, "devName");
            cJSON* devType = cJSON_GetObjectItemCaseSensitive(data, "devType");
            cJSON* devMacAddr = cJSON_GetObjectItemCaseSensitive(data, "devMacAddr");
            cJSON* firmwareVersion = cJSON_GetObjectItemCaseSensitive(data, "firmwareVersion");
            cJSON* hardwareVersion = cJSON_GetObjectItemCaseSensitive(data, "hardwareVersion");

            printf("Device Name: %s\n", devName ? devName->valuestring : "N/A");
            printf("Device Type: %d\n", devType ? devType->valueint : -1);
            printf("MAC Address: %s\n", devMacAddr ? devMacAddr->valuestring : "N/A");
            printf("Firmware Version: %s\n", firmwareVersion ? firmwareVersion->valuestring : "N/A");
            printf("Hardware Version: %s\n", hardwareVersion ? hardwareVersion->valuestring : "N/A");

            // Parse nested fields (e.g., sdcode, plantAi)
            cJSON* sdcode = cJSON_GetObjectItemCaseSensitive(data, "sdcode");
            if (sdcode) {
                cJSON* sdStatus = cJSON_GetObjectItemCaseSensitive(sdcode, "sdStatus");
                cJSON* capacity = cJSON_GetObjectItemCaseSensitive(sdcode, "capacity");
                cJSON* usage = cJSON_GetObjectItemCaseSensitive(sdcode, "usage");

                printf("SD Status: %d\n", sdStatus ? sdStatus->valueint : -1);
                printf("SD Capacity: %d MB\n", capacity ? capacity->valueint : -1);
                printf("SD Usage: %d MB\n", usage ? usage->valueint : -1);
            }

            // Add similar parsing for other nested fields (e.g., plantAi, autoPhoto)
        }

        cJSON_Delete(root);
        return 0;
    }
    if (strstr(json_response, "\"code\":200") != NULL) { printf("[SUCCESS] Command executed successfully\n"); return 0; }
    if (strstr(json_response, "\"ack\":true") != NULL) { printf("[INFO] Command acknowledged\n"); return 0; }
    return 0;
}

// ---------------- Record list implementation (migrated) ----------------
typedef struct {
    char start_time[64];
    char end_time[64];
    long long start_ts;
    long long end_ts;
    int rec_type;
    unsigned int size;
    int frame_rate;
    int code_type;
} RecordItem;

typedef struct {
    RecordItem* items;
    int count;
    int capacity;
    int selected_idx;
} RecordList;

static RecordList g_record_list = {NULL,0,0,-1};

void init_record_list(void) {
    if (g_record_list.items == NULL) {
        g_record_list.capacity = 100;
        g_record_list.items = (RecordItem*)malloc(sizeof(RecordItem) * g_record_list.capacity);
        g_record_list.count = 0;
        g_record_list.selected_idx = -1;
    }
}

void clear_record_list(void) { g_record_list.count = 0; g_record_list.selected_idx = -1; }

void destroy_record_list(void) { if (g_record_list.items) free(g_record_list.items); g_record_list.items = NULL; g_record_list.count = 0; g_record_list.capacity = 0; }

void add_record_item(const char* start_time, const char* end_time, int rec_type, long long start_ts, long long end_ts, unsigned int size, int frame_rate, int code_type) {
    if (g_record_list.count >= g_record_list.capacity) {
        g_record_list.capacity *= 2;
        g_record_list.items = (RecordItem*)realloc(g_record_list.items, sizeof(RecordItem) * g_record_list.capacity);
    }
    RecordItem* item = &g_record_list.items[g_record_list.count];
    strncpy(item->start_time, start_time, sizeof(item->start_time)-1);
    strncpy(item->end_time, end_time, sizeof(item->end_time)-1);
    item->start_ts = start_ts; item->end_ts = end_ts; item->rec_type = rec_type; item->size = size; item->frame_rate = frame_rate; item->code_type = code_type;
    g_record_list.count++;
}

// ---------------- Command callbacks (migrated) ----------------
static unsigned short s_global_pkg_id = 1;
static unsigned short s_global_seq = 0;
static const char* g_client_id = "Android_1c775ac30545f25a";
static const char* g_client_user = "29566628-5071-47e7-b5f5-9cc3849c9ade";

// Forward
int send_command(INT32 session_handle, const char* json_data, unsigned short pkg_id, unsigned short pkg_cmd) {
    unsigned char package[4096];
    int pkg_len = build_command_package(json_data, package, sizeof(package), pkg_id, pkg_cmd);
    if (pkg_len < 0) {
        printf("[Command] ERROR: Failed to build command package\n");
        return -1;
    }
    
    printf("[Command] ========== SENDING COMMAND ==========\n");
    printf("[Command] Package ID: 0x%04X\n", pkg_id);
    printf("[Command] Package Command: 0x%04X\n", pkg_cmd);
    printf("[Command] JSON Data: %s\n", json_data);
    printf("[Command] Package Length: %d bytes\n", pkg_len);
    printf("[Command] Package Hex: ");
    for (int i = 0; i < pkg_len && i < 64; i++) {
        printf("%02X ", package[i]);
    }
    if (pkg_len > 64) printf("...");
    printf("\n");
    
    INT32 ret = PPCS_Write(session_handle, 0, (char*)package, pkg_len);
    if (ret < 0) {
        printf("[Command] ERROR: PPCS_Write failed with code %d\n", ret);
        print_error("PPCS_Write", ret);
        return -1;
    }
    
    printf("[Command] SUCCESS: Sent %d bytes to session 0x%08X\n", ret, session_handle);
    printf("[Command] ======================================\n");
    return 0;
}

void on_live_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[Live] ERROR: Invalid context\n");
        return;
    }
    if (ctx->live_started) {
        printf("[Live] WARNING: Live stream already started\n");
        return;
    }
    printf("[Live] ========== LIVE START BUTTON CLICKED ==========\n");
    printf("[Live] Session Handle: 0x%08X\n", ctx->session_handle);
    
    char json_request[2048];
    snprintf(json_request, sizeof(json_request), "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_VIDEO_START\",\"id\":\"%s\",\"user\":\"%s\"}", s_global_seq++, JSON_CMD_VIDEO_START, g_client_id, g_client_user);
    printf("[Live] JSON: %s\n", json_request);
    
    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_VIDEO_START) == 0) {
        ctx->live_started = 1;
        printf("[Live] SUCCESS: Live stream started flag set\n");
    } else {
        printf("[Live] ERROR: Failed to send live start command\n");
    }
    printf("[Live] ==============================================\n");
}

void on_live_stop_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[Live] ERROR: Invalid context\n");
        return;
    }
    if (!ctx->live_started) {
        printf("[Live] WARNING: Live stream not started\n");
        return;
    }
    printf("[Live] ========== LIVE STOP BUTTON CLICKED ==========\n");
    printf("[Live] Session Handle: 0x%08X\n", ctx->session_handle);
    
    char json_request[2048];
    snprintf(json_request, sizeof(json_request), "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_VIDEO_STOP\",\"id\":\"%s\",\"user\":\"%s\"}", s_global_seq++, JSON_CMD_VIDEO_STOP, g_client_id, g_client_user);
    printf("[Live] JSON: %s\n", json_request);
    
    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_VIDEO_STOP) == 0) {
        if (ctx->video_mgr) {
            // Stop the main/live stream only: destroy decoder and close display
            video_manager_stop_stream(ctx->video_mgr, 1);
        }
        ctx->live_started = 0;
        printf("[Live] SUCCESS: Live stream stopped\n");
    } else {
        printf("[Live] ERROR: Failed to send live stop command\n");
    }
    printf("[Live] =============================================\n");
}

void on_playback_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[Playback] ERROR: Invalid context\n");
        return;
    }
    if (ctx->playback_started) {
        printf("[Playback] WARNING: Playback already started\n");
        return;
    }
    printf("[Playback] ========== PLAYBACK START BUTTON CLICKED ==========\n");
    printf("[Playback] Session Handle: 0x%08X\n", ctx->session_handle);
    
    char json_request[2048];
    long long start_ts = 0; long long end_ts = 0;
    snprintf(json_request, sizeof(json_request), "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_PLAYBACK_START\",\"id\":\"%s\",\"user\":\"%s\",\"data\":{\"startTime\":%lld,\"endTime\":%lld}}", s_global_seq++, JSON_CMD_PLAYBACK_START, g_client_id, g_client_user, start_ts, end_ts);
    printf("[Playback] JSON: %s\n", json_request);
    
    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_PLAYBACK_START) == 0) {
        ctx->playback_started = 1;
        printf("[Playback] SUCCESS: Playback started flag set\n");
    } else {
        printf("[Playback] ERROR: Failed to send playback start command\n");
    }
    printf("[Playback] =========================================================\n");
}

void on_record_list_button_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[RecordList] ERROR: Invalid context\n");
        return;
    }
    printf("[RecordList] ========== RECORD LIST BUTTON CLICKED ==========\n");
    printf("[RecordList] Session Handle: 0x%08X\n", ctx->session_handle);
    
    clear_record_list();
    char json_request[2048];
    time_t now = time(NULL);
    long long start_ts = now - 12*3600;
    long long end_ts = now;
    snprintf(json_request, sizeof(json_request), "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_RECORD_LIST_GET\",\"id\":\"%s\",\"user\":\"%s\",\"data\":{\"startTime\":%lld,\"endTime\":%lld}}", s_global_seq++, 0x207, g_client_id, g_client_user, start_ts, end_ts);
    printf("[RecordList] JSON: %s\n", json_request);
    printf("[RecordList] Query Time Range: %lld to %lld (last 12 hours)\n", start_ts, end_ts);
    
    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, 0x207) == 0) {
        printf("[RecordList] SUCCESS: Record list query sent\n");
    } else {
        printf("[RecordList] ERROR: Failed to send record list query\n");
    }
    printf("[RecordList] ====================================================\n");
}

void on_ota_upgrade_clicked(void* user_data) {
    printf("[OTA] OTA upgrade button clicked (not implemented yet)\n");
    (void)user_data;
}

void on_snapshot_img_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[Snapshot] ERROR: Invalid context\n");
        return;
    }
    printf("[Snapshot] ========== SNAPSHOT BUTTON CLICKED ==========\n");
    printf("[Snapshot] Session Handle: 0x%08X\n", ctx->session_handle);
    
    char json_request[512];
    snprintf(json_request, sizeof(json_request), "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_SNAPSHOT_IMG\",\"id\":\"%s\",\"user\":\"%s\"}", 
        s_global_seq++, JSON_CMD_SNAPSHOT_IMG, g_client_id, g_client_user);
    printf("[Snapshot] JSON: %s\n", json_request);
    
    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, JSON_CMD_SNAPSHOT_IMG) == 0) {
        printf("[Snapshot] SUCCESS: Snapshot command sent\n");
    } else {
        printf("[Snapshot] ERROR: Failed to send snapshot command\n");
    }
    printf("[Snapshot] ===============================================\n");
}

void on_get_device_config_clicked(void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[DeviceConfig] ERROR: Invalid context\n");
        return;
    }

    printf("[DeviceConfig] ========== GET DEVICE CONFIG BUTTON CLICKED =========="
           "\n[DeviceConfig] Session Handle: 0x%08X\n", ctx->session_handle);

    char json_request[512];
    snprintf(json_request, sizeof(json_request),
             "{\"version\":\"1.0\",\"ack\":false,\"seq\":%d,\"cmd\":%d,\"def\":\"JSON_CMD_SETTINGS_GET\",\"id\":\"%s\",\"user\":\"%s\"}",
             s_global_seq++, CMD_GET_DEVICE_CONFIG, g_client_id, g_client_user);

    printf("[DeviceConfig] JSON: %s\n", json_request);

    if (send_command(ctx->session_handle, json_request, s_global_pkg_id++, CMD_GET_DEVICE_CONFIG) == 0) {
        printf("[DeviceConfig] SUCCESS: Device config request sent\n");
    } else {
        printf("[DeviceConfig] ERROR: Failed to send device config request\n");
    }

    printf("[DeviceConfig] ===============================================\n");
}

void on_command_triggered(int command_id, void* user_data) {
    AppContext* ctx = (AppContext*)user_data;
    if (!ctx) {
        printf("[Command] ERROR: Invalid context for command 0x%X\n", command_id);
        return;
    }
    printf("[Command] ========== COMMAND TRIGGERED ==========\n");
    printf("[Command] Command ID: 0x%X\n", command_id);
    printf("[Command] Dispatching to handler...\n");
    
    switch (command_id) {
        case CMD_LIVE_START:
            printf("[Command] Handler: on_live_button_clicked\n");
            on_live_button_clicked(user_data);
            break;
        case CMD_LIVE_STOP:
            printf("[Command] Handler: on_live_stop_clicked\n");
            on_live_stop_clicked(user_data);
            break;
        case CMD_PLAYBACK_START:
            printf("[Command] Handler: on_playback_button_clicked\n");
            on_playback_button_clicked(user_data);
            break;
        case CMD_RECORD_LIST_GET:
            printf("[Command] Handler: on_record_list_button_clicked\n");
            on_record_list_button_clicked(user_data);
            break;
        case CMD_SNAPSHOT_IMG:
            printf("[Command] Handler: on_snapshot_img_clicked\n");
            on_snapshot_img_clicked(user_data);
            break;
        case CMD_GET_DEVICE_CONFIG:
            printf("[Command] Handler: on_get_device_config_clicked\n");
            on_get_device_config_clicked(user_data);
            break;
        default:
            printf("[Command] ERROR: Unknown command: 0x%X\n", command_id);
            break;
    }
    printf("[Command] ============================================\n");
}
