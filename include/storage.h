#ifndef STORAGE_H
#define STORAGE_H

#include "account.h"

int storage_save_account(const char *path,
                         const BankAccount *account);

int storage_load_account(const char *path,
                         BankAccount *account);

#endif