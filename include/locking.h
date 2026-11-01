
#ifndef LOCKING_H
#define LOCKING_H

/*
 * Returns a file descriptor with an exclusive lock acquired.
 * Returns -1 on failure.
 */
int banking_lock_acquire(const char *path);

/*
 * Releases the lock and closes the file descriptor.
 * Returns 0 on success, -1 on failure.
 */
int banking_lock_release(int fd);

#endif
