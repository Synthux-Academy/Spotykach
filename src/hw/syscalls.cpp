// Minimal newlib syscall stubs.
//
// There's no OS underneath this firmware, so none of these can do anything
// real. Without them, -lnosys supplies its own stubs that carry a linker
// warning ("_write is not implemented and will always fail", etc.) for every
// syscall the C library's *_r wrappers reference. Defining strong symbols
// here satisfies those references instead, silencing the warnings.
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C"
{
    int _close(int file)
    {
        (void)file;
        return -1;
    }

    int _fstat(int file, struct stat *st)
    {
        (void)file;
        st->st_mode = S_IFCHR;
        return 0;
    }

    int _isatty(int file)
    {
        (void)file;
        return 1;
    }

    _off_t _lseek(int file, _off_t offset, int whence)
    {
        (void)file;
        (void)offset;
        (void)whence;
        return 0;
    }

    _READ_WRITE_RETURN_TYPE _read(int file, void *buf, size_t len)
    {
        (void)file;
        (void)buf;
        (void)len;
        return 0;
    }

    _READ_WRITE_RETURN_TYPE _write(int file, const void *buf, size_t len)
    {
        (void)file;
        (void)buf;
        return len;
    }

    int _kill(pid_t pid, int sig)
    {
        (void)pid;
        (void)sig;
        errno = EINVAL;
        return -1;
    }

    pid_t _getpid(void)
    {
        return 1;
    }
}
