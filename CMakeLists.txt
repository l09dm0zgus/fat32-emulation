cmake_minimum_required(VERSION 3.26)
project(FAT32 C)

set(CMAKE_C_STANDARD 11)

add_executable(FAT32 main.c
        FAT32.h
        FAT32.c)
