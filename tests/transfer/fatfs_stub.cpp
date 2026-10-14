// FatFS stand-in for the host tests: files in a directory. It flags every read
// or write the SD DMA couldn't do: after the head of a partial sector FatFS moves
// whole sectors straight to buff + head, which must be word-aligned.
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "ff.h"
std::string g_root;
int g_dma_violations = 0;
static std::string P(const char* p) { return g_root + p; }
static void check(FIL* fp, const void* buff, UINT n) {
    auto pos = ftell(fp->f);
    UINT head = (512 - pos % 512) % 512;
    if (head > n) head = n;
    if (n - head >= 512 && ((uintptr_t)buff + head) % 4 != 0) g_dma_violations++;
}
FRESULT f_open(FIL* fp, const char* path, uint8_t mode) { fp->f = fopen(P(path).c_str(), (mode & FA_WRITE) ? "w+b" : "rb"); return fp->f ? FR_OK : FR_NO_FILE; }
FRESULT f_close(FIL* fp) { if (!fp->f) return FR_INVALID_OBJECT; fclose(fp->f); fp->f = nullptr; return FR_OK; }
FRESULT f_read(FIL* fp, void* b, UINT n, UINT* r) { check(fp, b, n); *r = fread(b, 1, n, fp->f); return FR_OK; }
FRESULT f_write(FIL* fp, const void* b, UINT n, UINT* w) { check(fp, b, n); *w = fwrite(b, 1, n, fp->f); return *w == n ? FR_OK : FR_DISK_ERR; }
FRESULT f_lseek(FIL* fp, FSIZE_t o) { return fseek(fp->f, o, SEEK_SET) == 0 ? FR_OK : FR_DISK_ERR; }
FSIZE_t f_size(FIL* fp) { auto c = ftell(fp->f); fseek(fp->f, 0, SEEK_END); auto s = ftell(fp->f); fseek(fp->f, c, SEEK_SET); return s; }
FSIZE_t f_tell(FIL* fp) { return ftell(fp->f); }
FRESULT f_stat(const char* p, FILINFO* i) { struct stat st; if (stat(P(p).c_str(), &st)) return FR_NO_FILE; i->fsize = st.st_size; return FR_OK; }
FRESULT f_unlink(const char* p) { return unlink(P(p).c_str()) == 0 ? FR_OK : FR_NO_FILE; }
FRESULT f_rename(const char* a, const char* b) { struct stat st; if (!stat(P(b).c_str(), &st)) return FR_EXIST; return rename(P(a).c_str(), P(b).c_str()) == 0 ? FR_OK : FR_DISK_ERR; }
FRESULT f_mkdir(const char* p) { struct stat st; if (!stat(P(p).c_str(), &st)) return FR_EXIST; return mkdir(P(p).c_str(), 0755) == 0 ? FR_OK : FR_NO_PATH; }
FRESULT f_getfree(const char*, DWORD* fre, FATFS** fs) { static FATFS f { 1000002, 64 }; *fs = &f; *fre = 500000; return FR_OK; }
FRESULT f_mount(FATFS*, const char*, uint8_t) { return FR_OK; }
