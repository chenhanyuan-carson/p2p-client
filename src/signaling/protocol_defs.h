#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>

/*
 * Unified Protocol Definitions
 * All package structures should be defined here to avoid duplication
 */

#pragma pack(1)

/* Common Package Header - used for all message types */
typedef struct {
    uint16_t s16PkgIdent;      /* Package identifier/type */
    uint16_t u16PkgLen;        /* Package data length (excluding header and tail) */
    uint16_t u16PkgId;         /* Unique package ID for reassembly */
    uint16_t u16PkgIndex;      /* Fragment index (0=last/complete) */
    uint16_t u16PkgKey;        /* Reserved key field */
    uint16_t u16PkgCmd;        /* Command type */
    uint8_t  u8PkgSubHead;     /* Sub-header type: 1=header present, 0=data only */
    uint8_t  u8Reserve[3];     /* Reserved bytes for alignment */
    uint64_t u64PkgUserData;   /* User data field */
} PackageHeader_t;

/* Common Package Tail */
typedef struct {
    uint8_t  u8Zero;           /* Always 0x00 */
    uint8_t  u8Res;            /* Reserved */
    uint16_t u16Check;         /* Checksum */
} PackageTail_t;

/* Video Stream Header (sub-header for video packets) */
typedef struct {
    char s8StreamType;         /* Stream type: 1=main, 2=secondary, 3=record, 4=intercom, 5=download */
    char s8EncodeType;         /* Encoding type (0=H264, 1=H265, 2=JPEG) */
    char s8FrameType;          /* Frame type: 1=I-frame, 2=P-frame */
    char s8Ch;                 /* Channel number */
    unsigned char u8Hour;      /* Hour */
    unsigned char u8Minute;    /* Minute */
    unsigned char u8Sec;       /* Second */
    unsigned char u8FrameRate; /* Frame rate */
    int s32FrameLen;           /* Frame length */
    unsigned short u16VideoWidth;  /* Video width */
    unsigned short u16VideoHeight; /* Video height */
    unsigned long long u64Pts;    /* Presentation timestamp */
} VideoStreamHeader_t;

/* Alias for backward compatibility */
typedef VideoStreamHeader_t TAG_PKG_VIDEO_HEADER_S;

/* Image Stream Header (sub-header for image packets) */
typedef struct {
    int8_t   s8ImageType;      /* Image type */
    int8_t   s8EncodeType;     /* Encoding type (1=JPG, 2=PNG) */
    int8_t   s8Ch;             /* Channel number */
    int8_t   s8Reserve;        /* Reserved */
    uint16_t u16Width;         /* Image width */
    uint16_t u16Height;        /* Image height */
    int32_t  s32ImageLen;      /* Total image length */
    uint64_t u64Pts;           /* Presentation timestamp */
} ImageStreamHeader_t;

#pragma pack()

/* Protocol Prefixes */
#define PKG_VIDEO_PREFIX_STR    "$gvi"   /* Video package prefix */
#define PKG_IMAGE_PREFIX_STR    "$gmi"   /* Image package prefix */
#define PKG_JSON_PREFIX_STR     "#nsj"   /* JSON command prefix */

/* Package Types */
#define PKG_TYPE_VIDEO      0x01     /* Video data */
#define PKG_TYPE_IMAGE      0x02     /* Image/snapshot data */
#define PKG_TYPE_JSON       0x03     /* JSON command */

#endif // PROTOCOL_DEFS_H
