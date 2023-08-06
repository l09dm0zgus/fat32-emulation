#include <stdio.h>
#include <ctype.h>
#include "FAT32.h"
static char currentPath[256] = "/";

static char* readCmd()
{
    printf("\n%s> ",currentPath);
    char* cmd = malloc(1024);
    if (!fgets(cmd, 1024, stdin))
    {
        free(cmd);
        return NULL;
    }
    const size_t len = strlen(cmd);
    if (cmd[len-1] == '\n')
        cmd[len-1] = 0; // Remove newline from the end of line
    return cmd;
}
u64 openDirectory(Fat32Context* context,const char* path);

int main(int argc, char** argv)
{
    Fat32Context* context = NULL;
    bool isFAT32 = true;
    char* diskName = "disk1.img";
    if(argc == 2)
    {
        diskName = argv[1];
        context = fat32Initialize(argv[1], &isFAT32);
    }
    else if (argc == 1)
    {
        context = fat32Create(diskName);
    }
    bool isRunning = true;
    while (isRunning)
    {
        char* input = readCmd();
        if (!input)
        {
            printf("\nNo command, exiting\n");
            break;
        }

        size_t cmdLength = 0;
        while (input[cmdLength] && !isspace(input[cmdLength]))
        {
            ++cmdLength;
        }

        if (!cmdLength)
        {
            free(input);
            continue;
        }

        char* cmd = malloc(cmdLength + 1);
        strncpy(cmd, input, cmdLength);
        cmd[cmdLength] = 0;

        if(strcmp(cmd, "exit") == 0 || strcmp(cmd, "e") == 0)
        {
            isRunning = false;
        }
        else if(strcmp(cmd,"format") == 0)
        {
            fat32Format(context,diskName);
        }
        if(context == NULL && !isFAT32)
        {
            printf("Unknown disk format.Please format disk\n");
        }
        else if(strcmp(cmd,"mkdir") == 0)
        {
            const char* arg = input + cmdLength + 1;
            fat32CreateDirectoryEntry(context,currentPath,arg,0,DIRENTRY_ATTR_DIRECTORY);
        }
        else if (strcmp(cmd, "ls") == 0)
        {
            fat32ListDirectory(context, openDirectory(context,currentPath));
        }
        else if(strcmp(cmd, "cd") == 0)
        {
            const char* arg = input + cmdLength + 1;

            if (arg[0] == '/' && arg[1] == '\0')
            {
                currentPath[0] = '/';
                currentPath[1] = '\0';
            }
            else
            {
                if(arg[0] == '/')
                {
                    strcpy(currentPath,arg);
                }
                else
                {
                    strcat(currentPath,arg);
                }
                u32 openedDirectory = openDirectory(context,arg);
                strcat(currentPath,"/");
            }
        }
        else if(strcmp(cmd,"touch") == 0)
        {
            const char* arg = input + cmdLength + 1;
            fat32CreateDirectoryEntry(context,currentPath,arg,256,DIRENTRY_ATTR_SYSTEM);
        }
        else if(strcmp(cmd,"help") == 0)
        {
            printf("help - show this.\n ls - show files. \n format - format disk to FAT32.\n mkdir <dir name> - create directory. \n cd <dir name> - open directory\n touch <file name> - create file\n");
        }
        else
        {
            printf("Unknown command!Please enter help to see commands.\n");
        }
    }

    fat32ContextCloseAndFree(&context);

    return 0;
}

u64 openDirectory(Fat32Context* context,const char* path)
{
    u64 addressToList = 0;
    if (path[0] == '/' && path[1] == '\0') // If root dir
    {
        addressToList = context->rootDirectoryAddress;
    }
    else
    {
        if(path[0] == '/')
        {
            path++;
        }
        DirectoryIteratorEntry* found = fat32OpenFile(context, path);
        if (!found)
        {
            fprintf(stderr, "Error: Directory '%s' not found\n", path);
        }
        else
        {
            if (!directoryEntryIsDirectory(found->entry))
            {
                fprintf(stderr, "Error: '%s' is not a directory\n", path);
                directoryIteratorEntryFree(&found);
            }
            if(found != NULL)
            {
                addressToList = directoryEntryGetDataAddress(context, found->entry);
                directoryIteratorEntryFree(&found);
            }
        }
    }
    return addressToList;
}

