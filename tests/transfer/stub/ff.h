// Host stand-in for the FatFS API used by Card, implemented in fatfs_stub.cpp.
#pragma once
#include <cstdint>
#include <cstdio>
typedef unsigned int UINT; typedef uint32_t DWORD; typedef uint64_t FSIZE_t;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE = 4, FR_NO_PATH = 5, FR_EXIST = 8, FR_INVALID_OBJECT = 9 } FRESULT;
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_ALWAYS 0x08
typedef struct { FILE* f; } FIL;
typedef struct { FSIZE_t fsize; } FILINFO;
typedef struct { DWORD n_fatent; uint32_t csize; } FATFS;
FRESULT f_open(FIL*, const char*, uint8_t); FRESULT f_close(FIL*);
FRESULT f_read(FIL*, void*, UINT, UINT*); FRESULT f_write(FIL*, const void*, UINT, UINT*);
FRESULT f_lseek(FIL*, FSIZE_t); FRESULT f_stat(const char*, FILINFO*);
FRESULT f_unlink(const char*); FRESULT f_rename(const char*, const char*); FRESULT f_mkdir(const char*);
FRESULT f_getfree(const char*, DWORD*, FATFS**); FRESULT f_mount(FATFS*, const char*, uint8_t);
FSIZE_t f_size(FIL*); FSIZE_t f_tell(FIL*);
