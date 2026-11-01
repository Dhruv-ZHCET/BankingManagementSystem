
#include "locking.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int banking_lock_acquire(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT, 0600);

    if (fd == -1)
        return -1;

    struct flock lock = {0};

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    /* Wait until the exclusive lock is available. */
    while (fcntl(fd, F_SETLKW, &lock) == -1) {
        if (errno == EINTR)
            continue;

        close(fd);
        return -1;
    }

    return fd;
}

int banking_lock_release(int fd)
{
    struct flock lock = {0};

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    int result;

    do {
        result = fcntl(fd, F_SETLK, &lock);
    } while (result == -1 && errno == EINTR);

    int close_result = close(fd);

    if (result == -1 || close_result == -1)
        return -1;

    return 0;
}
