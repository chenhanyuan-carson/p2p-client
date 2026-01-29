#ifndef PPCS_CORE_H
#define PPCS_CORE_H

#include <windows.h>
#include "PPCS_API.h"

#define CONFIG_FILE "config.conf"
#define MAX_CONFIG_VALUE_LEN 256

typedef struct {
    char InitString[MAX_CONFIG_VALUE_LEN];
    char TargetDID[MAX_CONFIG_VALUE_LEN];
    char ServerString[MAX_CONFIG_VALUE_LEN];
    int MaxNumSess;
    int SessAliveSec;
    int UDPPort;
    int ConnectionMode;
    int ReadTimeout;
    char APILogFile[MAX_CONFIG_VALUE_LEN];
} Config;

// Package node for queue
typedef struct PackageNode { unsigned char* data; int len; struct PackageNode* next; } PackageNode;

void init_config(Config *config);
int validate_config(Config *config);
void print_config(Config *config);
void print_api_info(void);
INT32 connect_to_device(const char* target_did, Config *config);
int ppcs_start_network(INT32 session_handle);
void ppcs_stop_network(void);
PackageNode* ppcs_pop_package(void);
void print_error(const char* function_name, INT32 error_code);

#endif // PPCS_CORE_H
