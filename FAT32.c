//
// Created by cx9ps3 on 04.08.2023.
//
#include "FAT32.h"
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#define PATH_SEP '/'

static inline u64 umin(u64 a, u64 b)
{
    return (a < b) ? a : b;
}

u32 BPBGetSectorCount(const BPB* input)
{
    // If sectorCount is 0, there are more than 65535 sectors,
    // so the number is stored in largeSectCount
    return input->sectorCount == 0 ? input->largeSectCount : input->sectorCount;
}

//------------------------------------------------------------------------------

u32 clusterPtrGetIndex(ClusterPtr ptr)
{
    return ptr & 0x0fffffff;
}

bool clusterPtrIsBadCluster(ClusterPtr ptr)
{
    return clusterPtrGetIndex(ptr) == 0x0ffffff7;
}

bool clusterPtrIsLastCluster(ClusterPtr ptr)
{
    return clusterPtrGetIndex(ptr) >= 0x0ffffff8;
}

bool clusterPtrIsNull(ClusterPtr ptr)
{
    return clusterPtrGetIndex(ptr) == 0;
}

u32 fatGetNextClusterPtr(const Fat32Context* cont, ClusterPtr current)
{
    const u32 fatOffset = clusterPtrGetIndex(current) * 4;
    return *(u32*)&cont->fat[fatOffset];
}
u32 findFreeCluster(Fat32Context* cont)
{
    u64 i, entry, totalClusters;
    u64 dataSectors = cont->ebpb->sectorsPerFat - cont->firstDataSector;

    u64 countOfClusters  = dataSectors / cont->bpb->sectorsPerClusters;

    for(i=0;i<totalClusters; i++)
    {
        entry = fatGetNextClusterPtr(cont,i);
        if((entry & 0x0fffffff) == 0) break;
    }
    return i;
}

bool directoryEntryIsVolumeLabel(const DirectoryEntry* entry)
{
    if (!directoryEntryIsLFE(entry->attributes)
        && (entry->attributes & (DIRENTRY_ATTR_VOLUME_ID | DIRENTRY_ATTR_DIRECTORY)) == DIRENTRY_ATTR_VOLUME_ID)
    {
        assert(entry->entryFirstClusterNum1 == 0);
        assert(entry->entryFirstClusterNum2 == 0);
        return true;
    }
    return false;
}

bool directoryEntryIsLFE(u8 attrs)
{
    return (attrs & DIRENTRY_MASK_LONG_NAME) == DIRENTRY_ATTR_LONG_NAME;
}

bool directoryEntryIsDirectory(const DirectoryEntry* entry)
{
    if (!directoryEntryIsLFE(entry->attributes)
        && (entry->attributes & (DIRENTRY_ATTR_VOLUME_ID | DIRENTRY_ATTR_DIRECTORY)) == DIRENTRY_ATTR_DIRECTORY)
    {
        assert(entry->fileSize == 0);
        return true;
    }
    return false;
}

bool directoryEntryIsFile(const DirectoryEntry* entry)
{
    return (!directoryEntryIsLFE(entry->attributes)
            && (entry->attributes & (DIRENTRY_ATTR_VOLUME_ID | DIRENTRY_ATTR_DIRECTORY))) == 0;
}

bool directoryEntryIsEmpty(const DirectoryEntry* entry)
{
    return clusterPtrIsNull(directoryEntryGetClusterPtr(entry));
}

u32 directoryEntryGetClusterPtr(const DirectoryEntry* entry)
{
    return ((u32)entry->entryFirstClusterNum1 << 16)
           | (u32)entry->entryFirstClusterNum2;
}

u32 directoryEntryGetFirstClusterNumber(const DirectoryEntry* input)
{
    return clusterPtrGetIndex(directoryEntryGetClusterPtr(input));
}

u64 directoryEntryGetDataAddress(const Fat32Context* cont, const DirectoryEntry* entry)
{
    return fat32GetFirstSectorOfCluster(cont, directoryEntryGetFirstClusterNumber(entry)) * cont->bpb->sectorSize;
}

char* directoryEntryAttrsToString(u8 attrs)
{
    char* str = malloc(7);

    if (directoryEntryIsLFE(attrs))
    {
        strncpy(str, "__LFE_", 7);
        return str;
    }

    str[0] = (attrs & DIRENTRY_ATTR_READONLY)  ? 'R' : '-';
    str[1] = (attrs & DIRENTRY_ATTR_HIDDEN)    ? 'H' : '-';
    str[2] = (attrs & DIRENTRY_ATTR_SYSTEM)    ? 'S' : '-';
    str[3] = (attrs & DIRENTRY_ATTR_VOLUME_ID) ? 'V' : '-';
    str[4] = (attrs & DIRENTRY_ATTR_DIRECTORY) ? 'D' : '-';
    str[5] = (attrs & DIRENTRY_ATTR_ARCHIVE)   ? 'A' : '-';
    str[6] = 0;
    return str;
}

int directoryEntryReadFileData(Fat32Context* cont, const DirectoryEntry* entry, u8* buffer, size_t bufferSize)
{
    assert(directoryEntryIsFile(entry));

    // Don't do anything if the file is empty
    // or it is on a bad cluster
    if (directoryEntryIsEmpty(entry) || clusterPtrIsBadCluster(directoryEntryGetClusterPtr(entry)))
    {
        return 0;
    }

    const u64 address = directoryEntryGetDataAddress(cont, entry);
    fseek(cont->file, address, SEEK_SET);
    return fread(buffer, 1, umin(bufferSize, entry->fileSize), cont->file);
}

DirectoryEntryTime toDirectoryEntryTime(u16 input)
{
    DirectoryEntryTime time =
            {
            .hour = (input & 0xf800) >> 11,
            .min  = (input & 0x07e0) >> 5,
            .sec  = (input & 0x001f) * 2
    };
    return time;
}

char* directoryEntryTimeToString(const DirectoryEntryTime* input)
{
    char* buffer = malloc(9);
    if (input->hour > 23
        || input->min > 59
        || input->sec > 59
        || snprintf(buffer, 9, "%02i:%02i:%02i", input->hour+1, input->min+1, input->sec) > 8)
    {
        // If no space for time, fill the output with ?s
        strcpy(buffer, "\?\?:\?\?:\?\?");
    }
    return buffer;
}


DirectoryEntryDate toDirectoryEntryDate(u16 input)
{
    DirectoryEntryDate date = {
            .year  = (input & 0xfe00) >> 9,
            .month = (input & 0x01e0) >> 5,
            .day   = (input & 0x001f)
    };
    return date;
}

char* directoryEntryDateToString(const DirectoryEntryDate* input)
{
    char* buffer = malloc(11);
    if (input->month == 0 || input->month > 12
        || input->day == 0 || input->day > 31
        || snprintf(buffer, 11, "%04i-%02i-%02i", 1980+input->year, input->month, input->day) > 10)
    {
        // If invalid, fill the output with ?s
        strcpy(buffer, "\?\?\?\?-\?\?-\?\?");
    }
    return buffer;
}

u16* lfeEntryGetNameUCS2(const LfeEntry* entry)
{
    uint16_t* buffer = calloc(LFE_ENTRY_NAME_LEN+1, 2);
    for (int i=0; i < 5; ++i)
    {
        if (entry->name0[i] != 0xff)
            buffer[i] = entry->name0[i];
    }
    for (int i=0; i < 6; ++i)
    {
        if (entry->name1[i] != 0xff)
            buffer[5+i] = entry->name1[i];
    }
    for (int i=0; i < 2; ++i)
    {
        if (entry->name2[i] != 0xff)
            buffer[11+i] = entry->name2[i];
    }
    return buffer;
}


char* lfeEntryGetNameASCII(const LfeEntry* entry)
{
    uint16_t* buffer = lfeEntryGetNameUCS2(entry);
    char* output = malloc(LFE_ENTRY_NAME_LEN+1);
    for (int i=0; i < LFE_ENTRY_NAME_LEN; ++i)
    {
        output[i] = buffer[i];
    }
    free(buffer);
    output[LFE_ENTRY_NAME_LEN] = 0;
    return output;
}

void directoryIteratorEntryFree(DirectoryIteratorEntry** entryP)
{
    free((*entryP)->entry);
    free((*entryP)->longFilename);
    free(*entryP);
    *entryP = NULL;
}

char* directoryIteratorEntryGetFileName(DirectoryIteratorEntry* entry)
{
    char* fileName;
    // If we have a long filename, return it
    if (entry->longFilename[0] != 0)
    {
        const size_t len = strlen(entry->longFilename);
        fileName = malloc(len+1);
        strcpy(fileName, entry->longFilename);
        fileName[len] = 0;
    }
        // Only remove padding when this is a file (directories don't have extensions)
    else if (directoryEntryIsFile(entry->entry))
    {
        // Count extension length
        int extLen = 0;
        for (int i=DIRENTRY_FILENAME_LEN-1; i >= 0; --i)
        {
            if (entry->entry->fileName[i] == ' ')
                break;
            ++extLen;
        }

        int outLen;
        int paddingCount = 0;
        { // Count length without padding spaces
            int i=DIRENTRY_FILENAME_LEN-1-extLen;
            // Skip padding spaces
            for (; i >= 0; --i)
            {
                if (entry->entry->fileName[i] != ' ')
                {
                    break;
                }
                ++paddingCount;
            }
        }
        // If we have an extension, leave space for the dot
        outLen = DIRENTRY_FILENAME_LEN-paddingCount+(extLen ? 1 : 0);

        fileName = malloc(outLen+1);
        // Copy string without padding spaces
        {
            // Copy extension
            for (int i=0; i < extLen; ++i)
                fileName[outLen-i-1] = entry->entry->fileName[DIRENTRY_FILENAME_LEN-1-i];

            // Put dot if there is an extension
            if (extLen)
                fileName[outLen-extLen-1] = '.';

            // Copy prefix
            for (int i=0; i < DIRENTRY_FILENAME_LEN-paddingCount-extLen; ++i)
                fileName[i] = entry->entry->fileName[i];
        }
        fileName[outLen] = 0;
    }
        // Directory without a long filename, return as it is
    else
    {
        fileName = malloc(DIRENTRY_FILENAME_LEN+1);
        strncpy(fileName, (const char*)entry->entry->fileName, DIRENTRY_FILENAME_LEN);
        fileName[DIRENTRY_FILENAME_LEN] = 0;
    }
    return fileName;
}

DirectoryIterator* directoryIteratorNew(u64 addr)
{
    DirectoryIterator* it = malloc(sizeof(DirectoryIterator));
    it->initAddress = addr;
    it->address = addr;
    it->longFilename = calloc(LFE_FULL_NAME_LEN + 1, 1);
    memset(it->lfeChecksums, 0, 16);
    return it;
}

static u8 calcShortNameChecksum(u8 name[DIRENTRY_FILENAME_LEN])
{
    uint8_t sum = 0;
    for (int i=0; i < DIRENTRY_FILENAME_LEN; ++i)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i];
    return sum;
}

DirectoryIteratorEntry* directoryIteratorNext(Fat32Context* cont,DirectoryIterator* it)
{
    if (it->address == 0) // If the iterator ended
    {
        return NULL;
    }

    DirectoryEntry* directory = malloc(sizeof(DirectoryEntry));
    assert(directory);
    const u32 clusterSizeBytes = cont->bpb->sectorsPerClusters * cont->bpb->sectorSize;
    while (true)
    {
        u64 newAddr = it->address + sizeof(DirectoryEntry);
        fseek(cont->file, it->address, SEEK_SET);
        fread(directory, sizeof(DirectoryEntry), 1, cont->file);

        if (newAddr % clusterSizeBytes == 0)
        {
            const ClusterPtr clusterI = (newAddr-cont->bpb->reservedSectorCount*cont->bpb->sectorSize)/clusterSizeBytes-cont->bpb->sectorSize+1;
            const ClusterPtr nextCluster = fatGetNextClusterPtr(cont, clusterI);
            assert(!clusterPtrIsNull(nextCluster));
            assert(!clusterPtrIsBadCluster(nextCluster));
            if (clusterPtrIsLastCluster(nextCluster))
            {
                it->address = 0;
                free(directory);
                return NULL;
            }
            else
            {
                newAddr = cont->rootDirectoryAddress+(nextCluster-cont->ebpb->rootDirectoryClusterNumber)*cont->bpb->sectorsPerClusters*cont->bpb->sectorSize;
            }
        }

        if (directory->fileName[0] == 0) // End of directory
        {
            it->address = 0;
            free(directory);
            return NULL;
        }

        if (directory->fileName[0] == 0xe5) // Unused entry, skip
        {
            continue;
        }

        if (directoryEntryIsLFE(directory->attributes)) // LFE Entry
        {
            const LfeEntry* lfeEntry = (LfeEntry*)directory;
            char* lfeVal = lfeEntryGetNameASCII(lfeEntry);
            const size_t fragI = (lfeEntry->nameStrIndex&0x0f)-1;

            if (it->lfeChecksums[fragI])
            {
                assert(false && "Duplicate LFE entry");
            }

            it->lfeChecksums[fragI] = lfeEntry->checksum;
            strncpy(it->longFilename + fragI * LFE_ENTRY_NAME_LEN, lfeVal, LFE_ENTRY_NAME_LEN);
            assert(it->longFilename[LFE_FULL_NAME_LEN] == 0);
            free(lfeVal);
        }
        else // Regular directory entry
        {
            DirectoryIteratorEntry* dirItEntry = malloc(sizeof(DirectoryIteratorEntry));
            assert(dirItEntry);
            dirItEntry->entry = directory;
            dirItEntry->address = it->address;
            dirItEntry->longFilename = calloc(LFE_FULL_NAME_LEN+1, 1);

            // Verify if the LFE entries have the correct checksum
            const u8 calcedChecksum = calcShortNameChecksum(dirItEntry->entry->fileName);
            bool lfeMismatch = false;
            for (int i=0; i < 16; ++i)
            {
                const u8 checksum = it->lfeChecksums[i];
                if (checksum && checksum != calcedChecksum)
                {
                    lfeMismatch = true;
                    break;
                }
            }

            // Throw away long filename on checksum mismatch
            if (lfeMismatch)
            {
                dirItEntry->longFilename[0] = 0;
            }
            else
            {
                strncpy(dirItEntry->longFilename, it->longFilename, LFE_FULL_NAME_LEN);
            }
            memset(it->longFilename, 0, LFE_FULL_NAME_LEN + 1);
            memset(it->lfeChecksums, 0, 16);
            it->address = newAddr;
            return dirItEntry;
        }

        it->address = newAddr;
    }
}

void directoryIteratorSetAddress(DirectoryIterator* it, uint64_t addr)
{
    it->initAddress = addr;
    it->address = addr;
}

void directoryIteratorRewind(DirectoryIterator* it)
{
    it->address = it->initAddress;
}

void directoryIteratorFree(DirectoryIterator** itP)
{
    if(*itP != NULL)
    {
        free((*itP)->longFilename);
    }
    free(*itP);
    *itP = NULL;
}

Fat32Context* fat32Initialize(const char* devFilePath,bool *isFAT32)
{
    Fat32Context* context = malloc(sizeof(Fat32Context));
    context->file = fopen(devFilePath, "rb+");
    if (!context->file)
    {
        printf("Failed to open file: %s: %s\n", devFilePath, strerror(errno));
        free(context);
        return NULL;
    }

    context->bpb = malloc(sizeof(BPB));
    fseek(context->file, 0, SEEK_SET);
    fread(context->bpb, sizeof(BPB), 1, context->file);
    context->isBpbModified = false;

    context->ebpb = malloc(sizeof(EBPB));
    fseek(context->file, sizeof(BPB), SEEK_SET);
    fread(context->ebpb, sizeof(EBPB), 1, context->file);
    context->isEbpbModified = false;

    context->fsinfo = malloc(sizeof(FSInfo));
    const u64 fsinfoStart = context->ebpb->fsInfoSectorNumber * context->bpb->sectorSize;
    fseek(context->file, fsinfoStart, SEEK_SET);
    fread(context->fsinfo, sizeof(FSInfo), 1, context->file);
    context->isFsinfoModified = false;

    context->fatSizeBytes = context->ebpb->sectorsPerFat * context->bpb->sectorSize;
    context->fat = malloc(context->fatSizeBytes);
    assert(context->fat);

    const u64 fatStart = context->bpb->reservedSectorCount * context->bpb->sectorSize;
    fseek(context->file, fatStart, SEEK_SET);

    fread(context->fat, 1, context->fatSizeBytes, context->file);
    context->isFatModified = false;

    context->firstDataSector = context->bpb->reservedSectorCount + (context->bpb->fatCount*context->ebpb->sectorsPerFat);
    context->rootDirectoryAddress = context->firstDataSector*context->bpb->sectorSize;
    u64 dataSectors = context->ebpb->sectorsPerFat - context->firstDataSector;

    u64 countOfClusters  = dataSectors / context->bpb->sectorsPerClusters;

    if(countOfClusters >= 65526)
    {
        *isFAT32 = true;
    }
    else
    {
        *isFAT32 = false;
    }

    return context;

}


Fat32Context* fat32Create(const char* devFilePath)
{
    Fat32Context* context = malloc(sizeof(Fat32Context));
    context->file = fopen(devFilePath, "w+b");

    if (!context->file)
    {
        printf("Failed to open device: %s: %s\n", devFilePath, strerror(errno));
        free(context);
        return NULL;
    }

    u64 diskSize = DISK_SIZE;
    for(int i = 0;i  < diskSize;i++)
    {
        fwrite("\0",sizeof(u8),1,context->file);
    }

    rewind(context->file);

    context->bpb = malloc(sizeof(BPB));

    u32 clusters;
    u32 clusterSize;

    u64 actualFatSize = diskSize/DEFAULT_SECTOR_SIZE;

    context->bpb->reserved0[0]=0xEB;
    context->bpb->reserved0[1]=0x58;
    context->bpb->reserved0[2]=0x90;
    memcpy(context->bpb->oemIdentifier,"MSDOS4.1",BPB_OEM_LEN);

    context->bpb->sectorSize = DEFAULT_SECTOR_SIZE;
    context->bpb->sectorsPerClusters = DEFAULT_SECTORS_PER_CLUSTER;
    context->bpb->reservedSectorCount = 32;//According to specification, they use 32
    context->bpb->fatCount = 2;
    context->bpb->directoryEntryCount = 0; //fat32 doesn't use this and it must be 0
    context->bpb->sectorCount = 0; //not for fat32
    context->bpb->mediaType = 0xF8; //fixed, non-removable drive*/
    context->bpb->sectorsPerFat = 0; //not for fat32
    context->bpb->headCount = 0;
    context->bpb->hiddenSectCount = 0;
    context->bpb->largeSectCount = actualFatSize;


    fwrite(context->bpb, sizeof(BPB), 1, context->file);
    context->isBpbModified = false;

    context->ebpb = malloc(sizeof(EBPB));
    clusters = (u32)((actualFatSize - 2)/( 1 + 2 * 4.0 / DEFAULT_SECTOR_SIZE));

    context->ebpb->sectorsPerFat = (clusters * 4 + DEFAULT_SECTOR_SIZE -1) / DEFAULT_SECTOR_SIZE;

    //this emulation  of fat32 ,so set dummy numbers

    context->ebpb->flags = 0x04; //no mirroring, and fat 0 is the active fat
    context->ebpb->fatVersion = 0; //fat32 version 0:0
    context->ebpb->rootDirectoryClusterNumber = 0x2; /*there are no clusters 0 and 1 */
    context->ebpb->fsInfoSectorNumber = 1;
    context->ebpb->backupSectorNumber = 6;

    memset(context->ebpb->reserved0, 0xA, 12);

    context->ebpb->driveNumber = 1;
    context->ebpb->ntFlags = 0x1 | 0x3 | 0xB;
    context->ebpb->signature = 0x29; //from fat specification indicate that the following fields are present
    context->ebpb->serialNumber = (u32)time(NULL); /*take a unique volume id*/

    memcpy(context->ebpb->label,"MSDOS 4.1  ",11);
    memcpy(context->ebpb->systemId,"FAT32   ",8);

    fseek(context->file, sizeof(BPB), SEEK_SET);
    fwrite(context->ebpb, sizeof(EBPB), 1, context->file);
    context->isEbpbModified = false;

    context->fsinfo = malloc(sizeof(FSInfo));

    /*make the fs_info structure*/
    context->fsinfo->leadSignature = FSINFO_LEAD_SIG;
    context->fsinfo->signature = FSINFO_SIG;
    context->fsinfo->freeCount = clusters - 1; //cluster 2 is used for the root directory
    context->fsinfo->nextFree = 3; //sectors start at number 2 is the rootCluster
    context->fsinfo->trailSig = FSINFO_TRAIL_SIG;

    memset(context->fsinfo->reserved0, 0xA, 480);
    memset(context->fsinfo->reserved1, 0xA, 12);

    const u64 fsinfoStart = context->ebpb->fsInfoSectorNumber * context->bpb->sectorSize;
    fseek(context->file, fsinfoStart, SEEK_SET);
    fwrite(context->fsinfo, sizeof(FSInfo), 1, context->file);
    context->isFsinfoModified = false;

    context->fatSizeBytes = context->ebpb->sectorsPerFat * context->bpb->sectorSize;
    context->fat = malloc(context->fatSizeBytes);

    assert(context->fat);

    const u64 fatStart = context->bpb->reservedSectorCount * context->bpb->sectorSize;
    fseek(context->file, fatStart, SEEK_SET);

    fwrite(context->fat, 1, context->fatSizeBytes, context->file);
    context->isFatModified = false;

    context->firstDataSector = context->bpb->reservedSectorCount + (context->bpb->fatCount*context->ebpb->sectorsPerFat);
    context->rootDirectoryAddress = context->firstDataSector * context->bpb->sectorSize;

    return context;

}
void fat32ContextCloseAndFree(Fat32Context** contextP)
{
    Fat32Context* context = *contextP;
    const u32 backupOffs = context->ebpb->backupSectorNumber*context->bpb->sectorSize;

    if (context->isBpbModified)
    {
        u64 pos = 0;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->bpb, sizeof(BPB), 1, context->file);

        // Write to backup sector
        pos += backupOffs;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->bpb, sizeof(BPB), 1, context->file);
    }

    if (context->isEbpbModified)
    {
        u64 pos = sizeof(BPB);
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->ebpb, sizeof(EBPB), 1, context->file);

        // Write to backup sector
        pos += backupOffs;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->ebpb, sizeof(EBPB), 1, context->file);
    }

    if (context->isFsinfoModified)
    {
        u64 pos = context->ebpb->fsInfoSectorNumber * context->bpb->sectorSize;
        fseek(context->file, pos, SEEK_SET);
        fread(context->fsinfo, sizeof(FSInfo), 1, context->file);

        // Write to backup sector
        pos += backupOffs;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->fsinfo, sizeof(FSInfo), 1, context->file);
    }

    if (context->isFatModified)
    {
        u64 pos = context->bpb->reservedSectorCount*context->bpb->sectorSize;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->fat, 1, context->fatSizeBytes, context->file);

        // Write to backup sector
        pos += backupOffs;
        fseek(context->file, pos, SEEK_SET);
        fwrite(context->fat, 1, context->fatSizeBytes, context->file);
    }

    fclose(context->file);
    free(context->bpb);
    free(context->ebpb);
    free(context->fat);
    free(context->fsinfo);
    free(context);
    *contextP = NULL;
}

uint32_t fat32GetFirstSectorOfCluster(const Fat32Context* cont, uint32_t cluster)
{
    return (cluster - 2) * cont->bpb->sectorsPerClusters + cont->firstDataSector;
}


void fat32ListDirectory(Fat32Context* cont, u64 addr)
{
    // Print table heading
    printf("%-11.11s  |  %50s  |  %10s  |  %s  |  %s\n",
          "FILE NAME", "LONG FILE NAME", "SIZE", "ATTRS.", "CREAT. DATE & TIME");
    for (int i=0; i < 116; ++i)
    {
        printf((i == 13 || i == 68 || i == 83 || i == 94) ? "|" : "-");
    }
    printf("\n");

    // List directory
    DirectoryIterator* it = directoryIteratorNew(addr);
    int fileCount = 0;
    while (true)
    {
        DirectoryIteratorEntry* dirEntry = directoryIteratorNext(cont, it);
        if (dirEntry == NULL)
            break;

        char* attrs = directoryEntryAttrsToString(dirEntry->entry->attributes);

        DirectoryEntryDate cDate = toDirectoryEntryDate(dirEntry->entry->creationDate);

        char* cDateStr = directoryEntryDateToString(&cDate);

        DirectoryEntryTime cTime = toDirectoryEntryTime(dirEntry->entry->creationTime);

        char* cTimeStr = directoryEntryTimeToString(&cTime);

        printf("%-11.11s  |  %50s  |  ", dirEntry->entry->fileName, dirEntry->longFilename);

        if (directoryEntryIsDirectory(dirEntry->entry))
        {
            printf("     <DIR>");
        }
        else
        {
            printf("%10i", dirEntry->entry->fileSize);
        }
        printf("  |  %s  |  %s %s\n", attrs, cDateStr, cTimeStr);

        free(attrs);
        free(cDateStr);
        free(cTimeStr);

        directoryIteratorEntryFree(&dirEntry);
        ++fileCount;
    }

    directoryIteratorFree(&it);
    printf("%i items in directory\n", fileCount);
}

static char* strtToUpper(const char* str)
{
    const size_t len = strlen(str);
    char* output = malloc(len+1);
    for (size_t i=0; i <= len; ++i)
        output[i] = toupper(str[i]);
    return output;
}

DirectoryIteratorEntry* fat32FindInDirectory(Fat32Context* cont, u64 addr, const char* toFind)
{
    DirectoryIterator* it = directoryIteratorNew(addr);
    char* toFindUpper = strtToUpper(toFind);
    while (true)
    {
        DirectoryIteratorEntry* result = directoryIteratorNext(cont, it);
        if (result == NULL)
        {
            free(toFindUpper);
            directoryIteratorFree(&it);
            return NULL;
        }

        char* entryFileName = directoryIteratorEntryGetFileName(result);
        char* entryFileNameUpper = strtToUpper(entryFileName);

        if (strcmp(entryFileNameUpper, toFindUpper) == 0)
        {
            free(toFindUpper);
            free(entryFileName);
            free(entryFileNameUpper);
            directoryIteratorFree(&it);
            return result;
        }

        directoryIteratorEntryFree(&result);
        free(entryFileName);
        free(entryFileNameUpper);
    }

    assert(false); // Unreachable
    return NULL;
}

static size_t findChar(const char* str, char c)
{
    const size_t len = strlen(str);
    for (size_t i=0; i < len; ++i)
    {
        if (str[i] == c)
            return i; // Found
    }
    return len; // Not found, end reached
}

static DirectoryIteratorEntry* findPath(Fat32Context* cont, const char* path, uint64_t parentAddr)
{
    const size_t pathLen = strlen(path);
    const size_t subpathLen = findChar(path, PATH_SEP);
    char* subpath = malloc(subpathLen+1);
    strncpy(subpath, path, subpathLen);
    subpath[subpathLen] = 0;

    DirectoryIteratorEntry* entry = fat32FindInDirectory(cont, parentAddr, subpath);
    if (entry == NULL)
    {
        free(subpath);
        return NULL;
    }
    const u64 addr = directoryEntryGetDataAddress(cont, entry->entry);

    free(subpath);

    const size_t nextSep = findChar(path, PATH_SEP);
    if (nextSep == pathLen) // If end of path
    {
        return entry; // The current entry is the result
    }
    directoryIteratorEntryFree(&entry);
    return findPath(cont, path+nextSep+1, addr);
}

void fat32Format(Fat32Context* context,const char* diskName)
{
    context = fat32Create(diskName);
    printf("Disk succesfully formated\n.");
}



void fat32CreateDirectoryEntry(Fat32Context* cont, const char* currentFolder,const char* entryName,u32 size,u8 attributes)
{
    DirectoryEntry directoryEntry;
    u64 address = 0;
    bool isEmpty = true;

    if(currentFolder[0] == '/' && currentFolder[1] =='\0')
    {
        address = cont->rootDirectoryAddress;
    }
    else
    {
        currentFolder++;
        DirectoryIteratorEntry* found = fat32OpenFile(cont, currentFolder);
        if(found)
        {
            address = directoryEntryGetDataAddress(cont, found->entry);
            directoryIteratorEntryFree(&found);
        }
    }

    DirectoryIterator* iterator = directoryIteratorNew(address);
    DirectoryIteratorEntry* result = NULL;
    while (true)
    {
        result = directoryIteratorNext(cont, iterator);
        if(result == NULL)
        {
            break;
        }
        address = result->address;
        isEmpty = false;
    }
    if(!isEmpty)
    {
        u64 newAddress =  address + sizeof(DirectoryEntry);
        fseek(cont->file, newAddress, SEEK_SET);
        if(result != NULL)
        {
            directoryIteratorEntryFree(result);
        }
        directoryIteratorFree(iterator);
    }
    else
    {
        fseek(cont->file, address, SEEK_SET);
    }

    u64 cluster = findFreeCluster(cont);
    strcpy_s(directoryEntry.fileName,11,entryName);
    directoryEntry.attributes = attributes;
    directoryEntry.ntReserved = 0;
    directoryEntry.creationTimeTenthSec = 0x25;
    directoryEntry.creationTime = 0x7e3c;
    directoryEntry.creationDate = 0x4262;
    directoryEntry.accessDate  = 0x4262;
    directoryEntry.entryFirstClusterNum1 = (cluster >> 16) & 0xffff;
    directoryEntry.modificationTime = 0x7e3c;
    directoryEntry.modificationDate = 0x4262;
    directoryEntry.entryFirstClusterNum2 = cluster & 0xffff;
    directoryEntry.fileSize = size;

    fwrite(&directoryEntry,sizeof(DirectoryEntry),1,cont->file);
}


DirectoryIteratorEntry* fat32OpenFile(Fat32Context* cont, const char* path)
{
    return findPath(cont, path, cont->rootDirectoryAddress);
}

static bool hasLower(const char* str)
{
    for (size_t i=0; i < strlen(str); ++i)
        if (islower(str[i]))
            return true;
    return false;
}

ChError fsRenameVolume(Fat32Context* cont, const char* name)
{
    const size_t nameLen = strlen(name);
    if (nameLen > EBPB_LABEL_LEN)
    {
        printf("New volume label is too long (%i chars), max is %i\n", nameLen, EBPB_LABEL_LEN);
        return ERROR_INVALID_ARG;
    }
    char* nameUpper = strtToUpper(name);
    if (hasLower(name))
    {
        printf("New volume label is not lowercase: '%s', using uppercase: '%s'\n", name, nameUpper);
    }

    uint8_t buffer[DIRENTRY_FILENAME_LEN];
    memset(buffer, ' ', DIRENTRY_FILENAME_LEN); // Write padding
    memcpy((char*)buffer, nameUpper, strlen(nameUpper)); // Copy string without null terminator

    // Change entry value in root directory
    {
        DirectoryIterator* it = directoryIteratorNew(cont->rootDirectoryAddress);
        DirectoryIteratorEntry* labelEntry;
        while (true)
        {
            labelEntry = directoryIteratorNext(cont, it);
            if (!labelEntry)
                break;

            if (directoryEntryIsVolumeLabel(labelEntry->entry))
                break;

            directoryIteratorEntryFree(&labelEntry);
        }
        directoryIteratorFree(&it);

        if (!labelEntry)
        {
            printf("BUG: Failed to find volume label entry\n");
            assert(false);
            abort();
        }

        fseek(cont->file, labelEntry->address, SEEK_SET);
        const size_t written = fwrite(buffer, 1, DIRENTRY_FILENAME_LEN, cont->file);
        assert(written == DIRENTRY_FILENAME_LEN);


        directoryIteratorEntryFree(&labelEntry);
    }

    // Change EBPB value
    {
        memcpy(cont->ebpb->label, buffer, DIRENTRY_FILENAME_LEN);
        cont->isEbpbModified = true;
    }

    free(nameUpper);
    return ERROR_OK;
}
