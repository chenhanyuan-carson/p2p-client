#include "ppcs_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "PPCS_API.h"
#include "PPCS_Error.h"
#include "protocol_defs.h"

// Use unified protocol definitions
typedef PackageHeader_t TAG_PKG_HEADER_S;
typedef PackageTail_t TAG_PKG_TAIL_S;

// Globals for package queue and network thread
typedef struct PackageNode { unsigned char* data; int len; struct PackageNode* next; } PackageNode;
static PackageNode* g_pkg_head = NULL; static PackageNode* g_pkg_tail = NULL; static CRITICAL_SECTION g_pkg_cs; static HANDLE g_pkg_event = NULL; static HANDLE g_net_thread = NULL; static volatile int g_net_thread_run = 0;

int read_config_value(const char* config_file, const char* key, char* value, int max_len) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        int key_len = eq - line; if (strncmp(line, key, key_len) == 0 && strlen(key) == key_len) {
            char *val_start = eq + 1; int val_len = strlen(val_start);
            while (val_len > 0 && (val_start[val_len - 1] == '\n' || val_start[val_len - 1] == '\r')) val_len--;
            if (val_len >= max_len) val_len = max_len - 1;
            strncpy(value, val_start, val_len); value[val_len] = '\0'; fclose(fp); return 1;
        }
    }
    fclose(fp); return 0;
}

void init_config(Config *config) {
    memset(config, 0, sizeof(Config));
    strcpy(config->InitString, "");
    strcpy(config->TargetDID, "");
    strcpy(config->ServerString, "");
    config->MaxNumSess = 5;
    config->SessAliveSec = 6;
    config->UDPPort = 0;
    config->ConnectionMode = 0x7A;
    config->ReadTimeout = 5000;
    strcpy(config->APILogFile, "");
    char value[256];
    if (read_config_value(CONFIG_FILE, "InitString", value, sizeof(value))) {
        strncpy(config->InitString, value, sizeof(config->InitString) - 1);
        config->InitString[sizeof(config->InitString) - 1] = '\0';
    }
    if (read_config_value(CONFIG_FILE, "TargetDID", value, sizeof(value))) {
        strncpy(config->TargetDID, value, sizeof(config->TargetDID) - 1);
        config->TargetDID[sizeof(config->TargetDID) - 1] = '\0';
    }
    if (read_config_value(CONFIG_FILE, "ServerString", value, sizeof(value))) {
        strncpy(config->ServerString, value, sizeof(config->ServerString) - 1);
        config->ServerString[sizeof(config->ServerString) - 1] = '\0';
    }
    if (read_config_value(CONFIG_FILE, "MaxNumSess", value, sizeof(value)))
        config->MaxNumSess = atoi(value);
    if (read_config_value(CONFIG_FILE, "SessAliveSec", value, sizeof(value)))
        config->SessAliveSec = atoi(value);
    if (read_config_value(CONFIG_FILE, "UDPPort", value, sizeof(value)))
        config->UDPPort = atoi(value);
    if (read_config_value(CONFIG_FILE, "ConnectionMode", value, sizeof(value)))
        config->ConnectionMode = (int)strtol(value, NULL, 0);
    if (read_config_value(CONFIG_FILE, "ReadTimeout", value, sizeof(value)))
        config->ReadTimeout = atoi(value);
    if (read_config_value(CONFIG_FILE, "APILogFile", value, sizeof(value))) {
        strncpy(config->APILogFile, value, sizeof(config->APILogFile) - 1);
        config->APILogFile[sizeof(config->APILogFile) - 1] = '\0';
    }
}

int validate_config(Config *config) { if (strlen(config->InitString)==0) { printf("[ERROR] InitString not configured in %s\n", CONFIG_FILE); return 0;} if (strlen(config->TargetDID)==0) { printf("[ERROR] TargetDID not configured in %s\n", CONFIG_FILE); return 0;} if (config->MaxNumSess <1 || config->MaxNumSess>512) { printf("[WARNING] MaxNumSess out of range, using default 5\n"); config->MaxNumSess=5;} if (config->SessAliveSec <6 || config->SessAliveSec >30) { printf("[WARNING] SessAliveSec out of range, using default 6\n"); config->SessAliveSec=6;} return 1; }

void print_config(Config *config) { printf("[Configuration Loaded]\n"); printf("  InitString: %s\n", config->InitString); printf("  TargetDID: %s\n", config->TargetDID); printf("  ServerString: %s\n", strlen(config->ServerString) > 0 ? config->ServerString : "(default server)"); printf("  MaxNumSess: %d\n", config->MaxNumSess); printf("  SessAliveSec: %d\n", config->SessAliveSec); printf("  ConnectionMode: 0x%02X\n", config->ConnectionMode); printf("  ReadTimeout: %d ms\n", config->ReadTimeout); if (strlen(config->APILogFile) > 0) printf("  APILogFile: %s\n", config->APILogFile); printf("\n"); }

const char* get_connection_mode(CHAR bMode) { switch(bMode) { case 0: return "LAN"; case 1: return "LAN-TCP"; case 2: return "P2P"; case 3: return "Relay"; case 4: return "TCP"; case 5: return "RP2P"; default: return "Unknown"; } }

const char* get_error_msg(INT32 error_code) { switch(error_code) { case ERROR_PPCS_SUCCESSFUL: return "Operation successful"; case ERROR_PPCS_NOT_INITIALIZED: return "PPCS not initialized"; case ERROR_PPCS_ALREADY_INITIALIZED: return "PPCS already initialized"; case ERROR_PPCS_TIME_OUT: return "Operation timeout"; case ERROR_PPCS_INVALID_ID: return "Invalid device ID"; case ERROR_PPCS_INVALID_PARAMETER: return "Invalid parameter"; case ERROR_PPCS_DEVICE_NOT_ONLINE: return "Device not online"; case ERROR_PPCS_SESSION_CLOSED_REMOTE: return "Session closed by remote"; case ERROR_PPCS_SESSION_CLOSED_TIMEOUT: return "Session closed by timeout"; case ERROR_PPCS_INVALID_SESSION_HANDLE: return "Invalid session handle"; default: return "Unknown error"; } }

void print_error(const char* function_name, INT32 error_code) { printf("[ERROR] %s: %s (code: %d)\n", function_name, get_error_msg(error_code), error_code); }

void print_api_info() { UINT32 version = PPCS_GetAPIVersion(); printf("=====================================\n"); printf("PPCS API Version: %d.%d.%d.%d\n", (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF); printf("=====================================\n\n"); }

// Package queue utilities
static void push_package_to_queue(unsigned char* data, int len) {
    PackageNode* node = (PackageNode*)malloc(sizeof(PackageNode)); if (!node) { free(data); return; }
    node->data = data; node->len = len; node->next = NULL;
    EnterCriticalSection(&g_pkg_cs);
    if (g_pkg_tail) { g_pkg_tail->next = node; g_pkg_tail = node; } else { g_pkg_head = g_pkg_tail = node; }
    LeaveCriticalSection(&g_pkg_cs);
    if (g_pkg_event) SetEvent(g_pkg_event);
}

static PackageNode* pop_package_from_queue(void) { PackageNode* node = NULL; EnterCriticalSection(&g_pkg_cs); if (g_pkg_head) { node = g_pkg_head; g_pkg_head = g_pkg_head->next; if (!g_pkg_head) g_pkg_tail = NULL; } LeaveCriticalSection(&g_pkg_cs); return node; }

// Network reader thread
static DWORD WINAPI network_reader_thread(LPVOID lpParam) {
    INT32 session_handle = (INT32)(intptr_t)lpParam;
    unsigned char* recv_buffer = (unsigned char*)malloc(1024*1024);
    if (!recv_buffer) {
        printf("[Network] ERROR: Failed to allocate receive buffer\n");
        return 0;
    }
    
    printf("[Network] ========== NETWORK THREAD STARTED ==========\n");
    printf("[Network] Session Handle: 0x%08X\n", session_handle);
    printf("[Network] Buffer Size: 1MB, Timeout: 500ms\n");
    printf("[Network] =============================================\n");
    
    int buffer_data_len = 0;
    g_net_thread_run = 1;
    unsigned long recv_count = 0;
    unsigned long pkg_count = 0;
    
    while (g_net_thread_run) {
        unsigned char temp_buffer[4096];
        INT32 read_len = sizeof(temp_buffer);
        INT32 ret = PPCS_Read(session_handle, 0, (char*)temp_buffer, &read_len, 500);
        
        if ((ret == ERROR_PPCS_SUCCESSFUL || ret == ERROR_PPCS_TIME_OUT) && read_len > 0) {
            recv_count++;
            /*printf("[Network] ========== DATA RECEIVED #%lu ==========\n", recv_count);
            printf("[Network] PPCS_Read returned: %d (SUCCESS=%d, TIMEOUT=%d)\n", ret, ERROR_PPCS_SUCCESSFUL, ERROR_PPCS_TIME_OUT);
            printf("[Network] Bytes received: %d\n", read_len);
            printf("[Network] Hex (first 64 bytes): ");
            for (int i = 0; i < read_len && i < 64; i++) {
                printf("%02X ", temp_buffer[i]);
            }
            if (read_len > 64) printf("...");
            printf("\n");
            printf("[Network] ASCII (first 64 bytes): ");
            for (int i = 0; i < read_len && i < 64; i++) {
                if (temp_buffer[i] >= 32 && temp_buffer[i] < 127) {
                    printf("%c", temp_buffer[i]);
                } else {
                    printf(".");
                }
            }
            printf("\n");*/
            
            if (buffer_data_len + read_len <= 1024*1024) {
                memcpy(recv_buffer + buffer_data_len, temp_buffer, read_len);
                buffer_data_len += read_len;
                printf("[Network] Buffer updated: total %d bytes\n", buffer_data_len);
            } else {
                printf("[Network] ERROR: Buffer overflow, resetting\n");
                buffer_data_len = 0;
                continue;
            }
            
            // Parse packages from buffer
            int parsed_count = 0;
            while (buffer_data_len >= 40) {
                int is_json = (memcmp(recv_buffer, "#nsj", 4) == 0);
                int is_video = (memcmp(recv_buffer, "$div", 4) == 0);
                int is_image = (memcmp(recv_buffer, "$gmi", 4) == 0);
                
                if (!is_json && !is_video && !is_image) {
                    printf("[Network] Invalid package header at offset 0, skipping 1 byte\n");
                    memmove(recv_buffer, recv_buffer + 1, buffer_data_len - 1);
                    buffer_data_len--;
                    continue;
                }
                
                if (buffer_data_len < 4 + sizeof(TAG_PKG_HEADER_S)) {
                    printf("[Network] Incomplete header, need %d bytes, have %d\n", 4 + (int)sizeof(TAG_PKG_HEADER_S), buffer_data_len);
                    break;
                }
                
                TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(recv_buffer + 4);
                int pkg_len = 4 + sizeof(TAG_PKG_HEADER_S) + header->u16PkgLen + sizeof(TAG_PKG_TAIL_S);
                
                printf("[Network] Package found: type=%s, id=0x%04X, cmd=0x%04X, len=%d, index=%d, total=%d\n",
                       is_json ? "JSON" : (is_video ? "VIDEO" : "IMAGE"),
                       header->u16PkgId, header->u16PkgCmd, header->u16PkgLen, header->u16PkgIndex, pkg_len);
                
                if (pkg_len <= 32 || pkg_len > 1024*1024) {
                    printf("[Network] ERROR: Invalid package length %d, skipping\n", pkg_len);
                    memmove(recv_buffer, recv_buffer + 4, buffer_data_len - 4);
                    buffer_data_len -= 4;
                    continue;
                }
                
                if (buffer_data_len < pkg_len) {
                    printf("[Network] Incomplete package, need %d bytes, have %d\n", pkg_len, buffer_data_len);
                    break;
                }
                
                unsigned char* pkg = (unsigned char*)malloc(pkg_len);
                if (pkg) {
                    memcpy(pkg, recv_buffer, pkg_len);
                    push_package_to_queue(pkg, pkg_len);
                    pkg_count++;
                    printf("[Network] Package #%lu queued successfully\n", pkg_count);
                } else {
                    printf("[Network] ERROR: Failed to allocate memory for package\n");
                }
                
                memmove(recv_buffer, recv_buffer + pkg_len, buffer_data_len - pkg_len);
                buffer_data_len -= pkg_len;
                parsed_count++;
            }
            
            if (parsed_count > 0) {
                printf("[Network] Parsed %d packages in this receive\n", parsed_count);
            }
            printf("[Network] =========================================\n");
        } else if (ret == ERROR_PPCS_TIME_OUT && read_len == 0) {
            // Timeout with no data - this is normal, just continue
            // printf("[Network] Read timeout (no data)\n");
        } else if (ret != ERROR_PPCS_SUCCESSFUL && ret != ERROR_PPCS_TIME_OUT) {
            printf("[Network] ERROR: PPCS_Read failed with code %d\n", ret);
            print_error("PPCS_Read", ret);
            // Session closed or other fatal error - exit thread
            if (ret == ERROR_PPCS_SESSION_CLOSED_REMOTE || 
                ret == ERROR_PPCS_SESSION_CLOSED_TIMEOUT || 
                ret == ERROR_PPCS_SESSION_CLOSED_CALLED) {
                printf("[Debug] Fatal error in PPCS_Read: %d. Attempting recovery.\n", ret);
                // Attempt recovery instead of exiting
                Sleep(1000); // Wait before retrying
                continue;
            } else {
                printf("[Debug] Non-fatal error in PPCS_Read: %d. Continuing thread.\n", ret);
                continue;
            }
        }
    }
    
    printf("[Network] ========== NETWORK THREAD STOPPED ==========\n");
    printf("[Network] Total received: %lu bytes\n", recv_count);
    printf("[Network] Total packages queued: %lu\n", pkg_count);
    printf("[Network] =============================================\n");
    
    free(recv_buffer);
    return 0;
}

// Connect helper
INT32 connect_to_device(const char* target_did, Config *config) {
    INT32 session_handle; st_PPCS_Session session_info;
    CHAR bEnableLanSearch = (CHAR)config->ConnectionMode;
    session_handle = PPCS_ConnectByServer(target_did, bEnableLanSearch, config->UDPPort, config->ServerString);
    if (session_handle < 0) { print_error("PPCS_ConnectByServer", session_handle); return -1; }
    INT32 ret = PPCS_Check(session_handle, &session_info);
    if (ret == ERROR_PPCS_SUCCESSFUL) { /* print info omitted */ } else { print_error("PPCS_Check", ret); PPCS_Close(session_handle); return -1; }
    return session_handle;
}

// Expose queue pop for main to consume
PackageNode* ppcs_pop_package(void) {
    PackageNode* node = pop_package_from_queue();
    if (node) {
        //printf("[Queue] ========== PACKAGE DEQUEUED ==========\n");
        //printf("[Queue] Package size: %d bytes\n", node->len);
        //printf("[Queue] Packet type: ");
        
        if (node->len >= 4) {
            if (memcmp(node->data, "#nsj", 4) == 0) {
                printf("JSON COMMAND\n");
                if (node->len >= 28) {
                    typedef struct {
                        unsigned short s16PkgIdent;
                        unsigned short u16PkgLen;
                        unsigned short u16PkgId;
                        unsigned short u16PkgIndex;
                        unsigned short u16PkgKey;
                        unsigned short u16PkgCmd;
                        unsigned char  u8PkgSubHead;
                        unsigned char  u8Reserve[3];
                        unsigned long long u64PkgUserData;
                    } TAG_PKG_HEADER_S;
                    TAG_PKG_HEADER_S* header = (TAG_PKG_HEADER_S*)(node->data + 4);
                    printf("[Queue] - Package ID: 0x%04X\n", header->u16PkgId);
                    printf("[Queue] - Package Command: 0x%04X\n", header->u16PkgCmd);
                    printf("[Queue] - Data Length: %d bytes\n", header->u16PkgLen);
                    
                    if (header->u16PkgLen > 0 && header->u16PkgLen < 8192) {
                        printf("[Queue] - Data: %s\n", (char*)(node->data + 4 + 28));
                    }
                }
            } else if (memcmp(node->data, "$div", 4) == 0) {
                //printf("VIDEO FRAME\n");
            } else if (memcmp(node->data, "$gmi", 4) == 0) {
                //printf("IMAGE DATA\n");
            } else {
                //printf("UNKNOWN\n");
            }
        }
        //printf("[Queue] ===================================\n");
    }
    return node;
}

// Start/stop network thread and init queue
int ppcs_start_network(INT32 session_handle) {
    InitializeCriticalSection(&g_pkg_cs);
    g_pkg_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    DWORD tid = 0;
    g_net_thread = CreateThread(NULL, 0, network_reader_thread, (LPVOID)(intptr_t)session_handle, 0, &tid);
    if (!g_net_thread) return -1;
    return 0;
}

void ppcs_stop_network(void) {
    if (g_net_thread) { g_net_thread_run = 0; if (g_pkg_event) SetEvent(g_pkg_event); WaitForSingleObject(g_net_thread, 2000); CloseHandle(g_net_thread); g_net_thread = NULL; }
    while (1) { PackageNode* n = pop_package_from_queue(); if (!n) break; if (n->data) free(n->data); free(n); }
    if (g_pkg_event) { CloseHandle(g_pkg_event); g_pkg_event = NULL; }
    DeleteCriticalSection(&g_pkg_cs);
}
