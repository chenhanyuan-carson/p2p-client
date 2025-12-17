/* Minimal cJSON.h for basic object/array/string/number parsing */
#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cJSON {
    struct cJSON *next,*prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)

cJSON *cJSON_Parse(const char *value);
void   cJSON_Delete(cJSON *c);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object,const char *string);
int    cJSON_IsArray(const cJSON *const item);
int    cJSON_IsNumber(const cJSON *const item);
int    cJSON_IsString(const cJSON *const item);
#define cJSON_ArrayForEach(element, array) \
    for(element = (array ? (array)->child : NULL); element != NULL; element = element->next)

#ifdef __cplusplus
}
#endif

#endif /* CJSON_H */
