#include "storage.h"

#include <fcntl.h>  //file i/o operation
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(fd,
                               buffer + written,
                               length - written);

        if (result < 0)
            return -1;

        if (result == 0)
            return -1;

        written += (size_t)result;
    }

    return 0;
}

int storage_save_account(const char *path,
                         const BankAccount *account)
{
    if (path == NULL || account == NULL)
        return -1;

    if (strchr(account->customer_name, '|') != NULL ||
        strchr(account->customer_name, '\n') != NULL)
        return -1;

    char buffer[128];

    int length = snprintf(buffer, sizeof(buffer),
                          "%d|%s|%lld|%d\n",
                          account->account_number,
                          account->customer_name,
                          account->balance_paise,
                          account->active ? 1 : 0);

    if (length < 0 || (size_t)length >= sizeof(buffer))
        return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd == -1)
        return -1;

    int result = write_all(fd, buffer, (size_t)length);

    if (close(fd) == -1)
        result = -1;

    return result;
}

int storage_load_account(const char *path,
                         BankAccount *account)
{
    if (path == NULL || account == NULL)
        return -1;

    int fd = open(path, O_RDONLY);

    if (fd == -1)
        return -1;

    char buffer[128];
    size_t used = 0;
    int failed = 0;

    while (used < sizeof(buffer) - 1) {
        ssize_t result = read(fd, buffer + used,
                              sizeof(buffer) - 1 - used);

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

    if (failed || used == sizeof(buffer) - 1 || used == 0)
        return -1;

    buffer[used] = '\0';

    int number;
    char name[50];
    long long balance;
    int active;
    int consumed = 0;

    if (sscanf(buffer, "%d|%49[^|]|%lld|%d\n%n",
               &number, name, &balance, &active,
               &consumed) != 4)
        return -1;

    if (consumed != (int)used || (active != 0 && active != 1))
        return -1;

    BankAccount loaded;

    if (!account_init(&loaded, number, name, balance))
        return -1;

    loaded.active = active != 0;
    *account = loaded;

    return 0;
}