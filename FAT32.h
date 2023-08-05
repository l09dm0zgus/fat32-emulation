//
// Created by cx9ps3 on 04.08.2023.
//
#ifndef FAT32_H
#define FAT32_H


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define DISK_SIZE (20 * (1024 * 1024))
#define DEFAULT_SECTOR_SIZE 512
#define DEFAULT_SECTORS_PER_CLUSTER 1

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t u16;
typedef uint8_t u8;

#define PACKED __attribute__((packed))

typedef enum bool
{
    false,
    true,
} bool;


typedef struct BPB BPB;
typedef struct EBPB EBPB;
typedef struct FSInfo FSInfo;
typedef struct DirectoryIteratorEntry DirectoryIteratorEntry;

typedef struct Fat32Context
{
    FILE* file;
    BPB* bpb;
    bool isBpbModified;
    EBPB* ebpb;
    bool isEbpbModified;
    FSInfo* fsinfo;
    bool  isFsinfoModified;
    u8* fat;
    u64 fatSizeBytes;
    bool isFatModified;
    u32 firstDataSector;
    u64 rootDirectoryAddress;
} Fat32Context;

Fat32Context* fat32Initialize(const char* devFilePath,bool *isFAT32);
Fat32Context* fat32Create(const char* devFilePath);

void fat32ContextCloseAndFree(Fat32Context** contextP);

uint32_t fat32GetFirstSectorOfCluster(const Fat32Context* cont, u32 cluster);
void fat32ListDirectory(Fat32Context* cont, u64 address);
DirectoryIteratorEntry* fat32FindInDirectory(Fat32Context* cont, u64 address, const char* toFind);
DirectoryIteratorEntry* fat32OpenFile(Fat32Context* cont, const char* path);
void fat32CreateDirectoryEntry(Fat32Context* cont, const char* currentFolder,const char* entryName,u32 size,u8 attributes);
void fat32Format(Fat32Context* context,const char* diskName);

#define BPB_OEM_LEN 8

/* BIOS Parameter Block */
typedef struct BPB
{
    u8 reserved0[3];
    u8 oemIdentifier[BPB_OEM_LEN];
    u16 sectorSize;
    u16 sectorsPerClusters;
    u16 reservedSectorCount;
    u8  fatCount;
    u16 directoryEntryCount;
    u16 sectorCount;
    u8 mediaType;
    u16 sectorsPerFat; // FAT12/FAT16 only, don't use
    u16 sectorsPerTrack;
    u16 headCount;
    u32 hiddenSectCount;
    u32 largeSectCount;
} PACKED BPB;

u32 BPBGetSectorCount(const BPB* input);

#define EBPB_LABEL_LEN 11
#define EBPB_SYS_ID_LEN 8

/* Extended BIOS Parameter Block */
typedef struct EBPB
{
    u32 sectorsPerFat;
    u16 flags;
    u16 fatVersion;
    u32 rootDirectoryClusterNumber;
    u16 fsInfoSectorNumber;
    u16 backupSectorNumber;
    u8 reserved0[12];
    u8 driveNumber;
    u8 ntFlags;
    u8 signature; // Must be 0x28 or 0x29
    u32 serialNumber;
    u8 label[EBPB_LABEL_LEN];
    u8 systemId[EBPB_SYS_ID_LEN];
    /* Boot code */
    /* MBR signature (0xAA55) */
} PACKED EBPB;

//------------------------------------------------------------------------------

#define FSINFO_LEAD_SIG     0x41615252
#define FSINFO_SIG          0x61417272
#define FSINFO_TRAIL_SIG    0xaa550000
#define FSINFO_NOT_KNOWN_FREE_CLUST_CNT  0xffffffff
#define FSINFO_NOT_KNOWN_NEXT_FREE_CLUST 0xffffffff

typedef struct FSInfo
{
    u32 leadSignature;
    u8 reserved0[480];
    u32 signature;
    u32 freeCount;
    u32 nextFree;
    u8 reserved1[12];
    u32 trailSig;
} PACKED FSInfo;

/*
 * 4 most significant bits: reserved.
 * 28 least significant bits: cluster index.
 */
typedef u32 ClusterPtr;

u32 clusterPtrGetIndex(ClusterPtr ptr);
bool clusterPtrIsBadCluster(ClusterPtr ptr);
bool clusterPtrIsLastCluster(ClusterPtr ptr);
bool clusterPtrIsNull(ClusterPtr ptr);
u32 fatGetNextClusterPtr(const Fat32Context* cont, ClusterPtr current);

#define DIRENTRY_FILENAME_LEN 11
#define DIRENTRY_ATTR_READONLY  (1 << 0)
#define DIRENTRY_ATTR_HIDDEN    (1 << 1)
#define DIRENTRY_ATTR_SYSTEM    (1 << 2)
#define DIRENTRY_ATTR_VOLUME_ID (1 << 3)
#define DIRENTRY_ATTR_DIRECTORY (1 << 4)
#define DIRENTRY_ATTR_ARCHIVE   (1 << 5)
#define DIRENTRY_ATTR_LONG_NAME       \
            ( DIRENTRY_ATTR_READONLY  \
            | DIRENTRY_ATTR_HIDDEN    \
            | DIRENTRY_ATTR_SYSTEM    \
            | DIRENTRY_ATTR_VOLUME_ID )
#define DIRENTRY_MASK_LONG_NAME       \
            ( DIRENTRY_ATTR_READONLY  \
            | DIRENTRY_ATTR_HIDDEN    \
            | DIRENTRY_ATTR_SYSTEM    \
            | DIRENTRY_ATTR_VOLUME_ID \
            | DIRENTRY_ATTR_DIRECTORY \
            | DIRENTRY_ATTR_ARCHIVE   )
typedef struct DirectoryEntry
{
    u8 fileName[DIRENTRY_FILENAME_LEN];
    u8 attributes;
    u8 ntReserved;
    u8 creationTimeTenthSec;
    u16 creationTime;
    u16 creationDate;
    u16 accessDate;
    u16 entryFirstClusterNum1;
    u16 modificationTime;
    u16 modificationDate;
    u16 entryFirstClusterNum2;
    u32 fileSize;
} PACKED DirectoryEntry;

bool directoryEntryIsVolumeLabel(const DirectoryEntry* entry);
bool directoryEntryIsLFE(u8 attrs);
bool directoryEntryIsDirectory(const DirectoryEntry* entry);
bool directoryEntryIsFile(const DirectoryEntry* entry);
bool directoryEntryIsEmpty(const DirectoryEntry* entry);
ClusterPtr directoryEntryGetClusterPtr(const DirectoryEntry* entry);
u32 directoryEntryGetFirstClusterNumber(const DirectoryEntry* input);
u64 directoryEntryGetDataAddress(const Fat32Context* cont, const DirectoryEntry* entry);
char* directoryEntryAttrsToString(u8 attrs);
u32 findFreeCluster(Fat32Context* cont);

typedef struct DirectoryEntryTime
{
    u32 hour;
    u32 min;
    u32 sec;
} PACKED DirectoryEntryTime;

DirectoryEntryTime toDirectoryEntryTime(u16 input);
char* directoryEntryTimeToString(const DirectoryEntryTime* input);

typedef struct DirectoryEntryDate
{
    u64 year;
    u64 month;
    u64 day;
} PACKED DirectoryEntryDate;

DirectoryEntryDate toDirectoryEntryDate(uint16_t input);
char* directoryEntryDateToString(const DirectoryEntryDate* input);


#define LFE_ENTRY_NAME_LEN 13
#define LFE_FULL_NAME_LEN LFE_ENTRY_NAME_LEN*16
typedef struct LfeEntry
{
    u8 nameStrIndex;
    u16 name0[5];
    u8 attributes; // 0x0F - LFE attribute
    u8 type; // Long entry type, should be 0 for file names
    u8 checksum;
    u16 name1[6];
    u16 alwaysZero;
    u16 name2[2];
} PACKED LfeEntry;

u16* lfeEntryGetNameUCS2(const LfeEntry* entry);
char* lfeEntryGetNameASCII(const LfeEntry* entry);


typedef struct DirectoryIteratorEntry
{
    DirectoryEntry* entry;
    char* longFilename;
    // Address of the entry itself, not where it points to
    u64 address;
} DirectoryIteratorEntry;

void directoryIteratorEntryFree(DirectoryIteratorEntry** entryP);
char* directoryIteratorEntryGetFileName(DirectoryIteratorEntry* entry);

typedef struct DirectoryIterator
{
    u64 address;
    u64 initAddress;
    char* longFilename;
    u8 lfeChecksums[16];
} DirectoryIterator;

DirectoryIterator* directoryIteratorNew(u64 address);
DirectoryIteratorEntry* directoryIteratorNext(Fat32Context* cont, DirectoryIterator* it);
void directoryIteratorSetAddress(DirectoryIterator* it, u64 address);
void directoryIteratorRewind(DirectoryIterator* it);
void directoryIteratorFree(DirectoryIterator** itP);

typedef enum
{
    ERROR_OK,
    ERROR_INVALID_ARG,
} ChError;

ChError fsRenameVolume(Fat32Context* cont, const char* name);

#endif //FAT32_H

