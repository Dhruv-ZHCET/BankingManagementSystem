#ifndef JOURNAL_H
#define JOURNAL_H

#include <stddef.h>

#define MAX_JOURNAL_TRANSACTIONS 10000
#define MAX_TRANSACTION_ID 64

typedef struct {
    char transaction_id[MAX_TRANSACTION_ID];
    int account_number;
    long long old_balance;
    long long new_balance;
    int second_account_number;
    long long second_old_balance;
    long long second_new_balance;
    int is_transfer;
    int committed;
} JournalTransaction;

int journal_prepare(const char *path,
                    const char *transaction_id,
                    int account_number,
                    long long old_balance,
                    long long new_balance);

int journal_prepare_transfer(const char *path,
                             const char *transaction_id,
                             int from_account,
                             long long old_from,
                             long long new_from,
                             int to_account,
                             long long old_to,
                             long long new_to);

int journal_commit(const char *path,
                   const char *transaction_id);

int journal_load(const char *path,
                 JournalTransaction transactions[],
                 size_t capacity,
                 size_t *count);

#endif