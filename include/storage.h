#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include "account.h"

#define MAX_ACCOUNTS 100

int storage_load_accounts(const char *path,
                          BankAccount accounts[],
                          size_t capacity,
                          size_t *count);

int storage_save_accounts(const char *path,
                          const BankAccount accounts[],
                          size_t count);

#endif