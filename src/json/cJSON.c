/* Migrated cJSON.c into src/json */
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void skip_ws(const char **p) { while (**p && isspace(**p)) (*p)++; }

static cJSON *new_item(void) {
    cJSON *item = (cJSON*)calloc(1, sizeof(cJSON));
    return item;
}

static cJSON *parse_value(const char **p);

static cJSON *parse_string(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;
    const char *start = *p;
    while (**p && **p != '"') (*p)++;
    size_t len = *p - start;
    char *out = (char*)malloc(len+1);
    if (out) {
        memcpy(out, start, len);
        out[len] = '\0';
    }
    if (**p == '"') (*p)++;
    cJSON *item = new_item();
    item->type = cJSON_String;
    item->valuestring = out;
    return item;
}

static cJSON *parse_number(const char **p) {
    char *endptr;
    double val = strtod(*p, &endptr);
    if (endptr == *p) return NULL;
    cJSON *item = new_item();
    item->type = cJSON_Number;
    item->valuedouble = val;
    item->valueint = (int)val;
    *p = endptr;
    return item;
}

static cJSON *parse_array(const char **p) {
    if (**p != '[') return NULL;
    (*p)++;
    cJSON *array = new_item();
    array->type = cJSON_Array;
    cJSON *last = NULL;
    skip_ws(p);
    if (**p == ']') { (*p)++; return array; }
    while (**p) {
        skip_ws(p);
        cJSON *item = parse_value(p);
        if (!item) break;
        if (!array->child) {
            array->child = item;
        } else {
            last->next = item;
            item->prev = last;
        }
        last = item;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == ']') { (*p)++; break; }
    }
    return array;
}

static cJSON *parse_object(const char **p) {
    if (**p != '{') return NULL;
    (*p)++;
    cJSON *object = new_item();
    object->type = cJSON_Object;
    cJSON *last = NULL;
    skip_ws(p);
    if (**p == '}') { (*p)++; return object; }
    while (**p) {
        skip_ws(p);
        cJSON *key = parse_string(p);
        skip_ws(p);
        if (**p != ':') { cJSON_Delete(key); break; }
        (*p)++;
        skip_ws(p);
        cJSON *val = parse_value(p);
        if (!val) { cJSON_Delete(key); break; }
        val->string = key->valuestring; // take ownership
        free(key);
        if (!object->child) {
            object->child = val;
        } else {
            last->next = val;
            val->prev = last;
        }
        last = val;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        if (**p == '}') { (*p)++; break; }
    }
    return object;
}

static cJSON *parse_value(const char **p) {
    skip_ws(p);
    if (**p == '{') return parse_object(p);
    if (**p == '[') return parse_array(p);
    if (**p == '"') return parse_string(p);
    if (**p == '-' || isdigit(**p)) return parse_number(p);
    if (strncmp(*p, "true", 4) == 0) { *p += 4; cJSON *i = new_item(); i->type = cJSON_True; i->valueint = 1; return i; }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; cJSON *i = new_item(); i->type = cJSON_False; i->valueint = 0; return i; }
    if (strncmp(*p, "null", 4) == 0) { *p += 4; cJSON *i = new_item(); i->type = cJSON_NULL; return i; }
    return NULL;
}

cJSON *cJSON_Parse(const char *value) {
    if (!value) return NULL;
    const char *p = value;
    skip_ws(&p);
    cJSON *item = parse_value(&p);
    return item;
}

void cJSON_Delete(cJSON *c) {
    if (!c) return;
    cJSON *next;
    if (c->child) {
        cJSON *child = c->child;
        while (child) {
            next = child->next;
            cJSON_Delete(child);
            child = next;
        }
    }
    if (c->valuestring) free(c->valuestring);
    if (c->string) free(c->string);
    free(c);
}

// 辅助函数：去除前后空白
static const char* cjson_trim(const char* s, int* len) {
    while (*s && (unsigned char)*s <= 32) s++;
    const char* e = s + strlen(s);
    while (e > s && (unsigned char)e[-1] <= 32) e--;
    if (len) *len = (int)(e - s);
    return s;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string) {
    if (!object || object->type != cJSON_Object) return NULL;
    cJSON *child = object->child;
    int keylen = 0;
    const char* key = cjson_trim(string, &keylen);
    while (child) {
        if (child->string) {
            int clen = 0;
            const char* ckey = cjson_trim(child->string, &clen);
            if (clen == keylen && strncmp(ckey, key, keylen) == 0) return child;
        }
        child = child->next;
    }
    return NULL;
}

int cJSON_IsArray(const cJSON *const item) { return item && item->type == cJSON_Array; }
int cJSON_IsNumber(const cJSON *const item) { return item && item->type == cJSON_Number; }
int cJSON_IsString(const cJSON *const item) { return item && item->type == cJSON_String; }
