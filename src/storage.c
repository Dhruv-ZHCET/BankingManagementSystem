#include "storage.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILE_SIZE (64 * 1024)

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(fd, buffer + written,
                               length - written);

        if (result < 0)
            return -1;

        if (result == 0)
            return -1;

        written += (size_t)result;
    }

    return 0;
}

static int parse_account(char *line, BankAccount *account)
{
    int number;
    char name[50];
    long long balance;
    int active;
    int consumed = 0;

    if (sscanf(line, "%d|%49[^|]|%lld|%d%n",
               &number, name, &balance, &active,
               &consumed) != 4)
        return -1;

    if (line[consumed] != '\0')
        return -1;

    if (active != 0 && active != 1)
        return -1;

    if (!account_init(account, number, name, balance))
        return -1;

    account->active = active != 0;
    return 0;
}

int storage_load_accounts(const char *path,
                          BankAccount accounts[],
                          size_t capacity,
                          size_t *count)
{
    if (path == NULL || accounts == NULL || count == NULL)
        return -1;

    *count = 0;

    int fd = open(path, O_RDONLY);

    if (fd == -1)
        return -1;

    char buffer[MAX_FILE_SIZE + 1];
    size_t used = 0;
    int failed = 0;

    while (used < MAX_FILE_SIZE) {
        ssize_t result = read(fd, buffer + used,
                              MAX_FILE_SIZE - used);

        if (result < 0) {
            failed = 1;
            break;
        }

        if (result == 0)
            break;

        used += (size_t)result;
    }

    if (close(fd) == -1)
        failed = 1;

    if (failed || used == MAX_FILE_SIZE)
        return -1;

    buffer[used] = '\0';

    size_t loaded = 0;
    char *line = buffer;

    while (*line != '\0') {
        char *newline = strchr(line, '\n');

        if (newline != NULL)
            *newline = '\0';

        if (*line != '\0') {
            if (loaded >= capacity ||
                parse_account(line, &accounts[loaded]) != 0)
                return -1;

            loaded++;
        }

        if (newline == NULL)
            break;

        line = newline + 1;
    }

    *count = loaded;
    return 0;
}


int storage_save_accounts(const char *path,
                          const BankAccount accounts[],
                          size_t count)
{
    if (path == NULL ||
        (accounts == NULL && count != 0) ||
        count > MAX_ACCOUNTS)
        return -1;

    /*
     * This simple implementation expects a path such as
     * "data/accounts.txt".
     */
    char temp_path[512];

    int path_length = snprintf(temp_path, sizeof(temp_path),
                               "%s.tmp", path);

    if (path_length < 0 ||
        (size_t)path_length >= sizeof(temp_path))
        return -1;

    int fd = open(temp_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  0600);

    if (fd == -1)
        return -1;

    int failed = 0;

    for (size_t i = 0; i < count; i++) {
        if (strchr(accounts[i].customer_name, '|') != NULL ||
            strchr(accounts[i].customer_name, '\n') != NULL) {
            failed = 1;
            break;
        }

        char line[128];

        int length = snprintf(line, sizeof(line),
                              "%d|%s|%lld|%d\n",
                              accounts[i].account_number,
                              accounts[i].customer_name,
                              accounts[i].balance_paise,
                              accounts[i].active ? 1 : 0);

        if (length < 0 ||
            (size_t)length >= sizeof(line) ||
            write_all(fd, line, (size_t)length) != 0) {
            failed = 1;
            break;
        }
    }

    /*
     * Ask the operating system to synchronize file data
     * before making the new file visible.
     */
    if (!failed && fsync(fd) == -1)
        failed = 1;

    if (close(fd) == -1)
        failed = 1;

    if (failed) {
        unlink(temp_path);
        return -1;
    }
    
    /*
     * Atomically replace the destination pathname.
     * The temporary file must be on the same filesystem.
     */
    if (rename(temp_path, path) == -1) {
        unlink(temp_path);
        return -1;
    }

    return 0;
}
