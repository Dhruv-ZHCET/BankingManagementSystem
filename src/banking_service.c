#include <pthread.h>
#include <stddef.h>

#include "account.h"
#include "storage.h"
#include "locking.h"
#include "banking_service.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>

#include "journal.h"
#include "recovery.h"

#define ACCOUNTS_FILE "data/accounts.txt"
#define LOCK_FILE "data/accounts.lock"

static pthread_mutex_t banking_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long transaction_counter = 0;

static int make_transaction_id(char *buffer, size_t size, const char *prefix)
{
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC)
        return -1;

    unsigned long counter = ++transaction_counter;
    int length = snprintf(buffer, size, "%s-%ld-%lld-%09ld-%lu",
                          prefix, (long)getpid(), (long long)now.tv_sec,
                          now.tv_nsec, counter);
    if (length < 0 || (size_t)length >= size)
        return -1;
    return 0;
}

static int banking_change_balance(int account_number,
                                  long long amount_paise,
                                  int is_deposit,
                                  long long *new_balance_paise)
{
    if (account_number <= 0 ||
        amount_paise <= 0 ||
        new_balance_paise == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&banking_mutex) != 0)
        return -1;

    int lock_fd = banking_lock_acquire(LOCK_FILE);

    if (lock_fd < 0) {
        pthread_mutex_unlock(&banking_mutex);
        return -1;
    }

    BankAccount accounts[MAX_ACCOUNTS];
    size_t count = 0;
    int result = -1;

    if (recovery_run(ACCOUNTS_FILE, "data/transactions.log") != 0)
        goto cleanup;

    if (storage_load_accounts(
            ACCOUNTS_FILE, accounts, MAX_ACCOUNTS, &count) != 0) {
        goto cleanup;
    }

    //checking what is the new balance that i want to update in the file
    for (size_t i = 0; i < count; i++) {
        if (accounts[i].account_number != account_number)
            continue;

        if (!accounts[i].active)
            goto cleanup;

        long long old_balance = accounts[i].balance_paise;
        long long new_balance;

        if (is_deposit) {
            if (amount_paise > LLONG_MAX - old_balance)
                goto cleanup;

            new_balance = old_balance + amount_paise;
        } else {
            if (amount_paise > old_balance)
                goto cleanup;

            new_balance = old_balance - amount_paise;
        }

        char transaction_id[64];

        if (make_transaction_id(transaction_id, sizeof(transaction_id), "TXN") != 0)
            goto cleanup;

        if (journal_prepare(
                "data/transactions.log",
                transaction_id,
                account_number,
                old_balance,
                new_balance) != 0) {
            goto cleanup;
        }

        /* Once PREPARED is durable, a later failure may require recovery. */
        result = BANKING_OUTCOME_UNKNOWN;
        accounts[i].balance_paise = new_balance;

        if (storage_save_accounts(
                ACCOUNTS_FILE, accounts, count) != 0) {
            /*
             * A PREPARED record now exists.
             * Do not report success.
             * Recovery is required before safely continuing.
             */
            goto cleanup;
        }

        if (journal_commit(
                "data/transactions.log", transaction_id) != 0) {
            /*
             * The balance was saved, but COMMITTED wasn't confirmed.
             * Do not blindly repeat the transaction.
             */
            goto cleanup;
        }

        *new_balance_paise = new_balance;
        result = 0;
        break;
    }

cleanup:
    banking_lock_release(lock_fd);
    pthread_mutex_unlock(&banking_mutex);

    return result;
}

int banking_deposit(int account_number,
                    long long amount_paise,
                    long long *new_balance_paise)
{
    return banking_change_balance(
        account_number, amount_paise, 1, new_balance_paise);
}

int banking_withdraw(int account_number,
                     long long amount_paise,
                     long long *new_balance_paise)
{
    return banking_change_balance(
        account_number, amount_paise, 0, new_balance_paise);
}

int banking_get_balance(int account_number,
                        long long *balance_paise)
{
    if (account_number <= 0 || balance_paise == NULL)
        return -1;

    // Step 1: Serialize banking operations within this process.
    if (pthread_mutex_lock(&banking_mutex) != 0)
        return -1;

    int lock_fd = banking_lock_acquire(LOCK_FILE);

    if (lock_fd < 0) {
        pthread_mutex_unlock(&banking_mutex);
        return -1;
    }

    BankAccount accounts[MAX_ACCOUNTS];
    size_t count = 0;
    int result = -1;

    if (recovery_run(ACCOUNTS_FILE, "data/transactions.log") != 0)
        goto cleanup;

    // Step 2: Load the latest account records.
    if (storage_load_accounts(
            ACCOUNTS_FILE, accounts, MAX_ACCOUNTS, &count) != 0) {
        goto cleanup;
    }

    // Step 3: Find the requested account.
    for (size_t i = 0; i < count; i++) {
        if (accounts[i].account_number == account_number &&
            accounts[i].active) {

            *balance_paise = accounts[i].balance_paise;
            result = 0;
            break;
        }
    }

cleanup:
    // Step 4: Release both locks on every exit path.
    banking_lock_release(lock_fd);
    pthread_mutex_unlock(&banking_mutex);

    return result;
}

int banking_transfer(int from_account_number,
                     int to_account_number,
                     long long amount_paise,
                     long long *from_balance_paise,
                     long long *to_balance_paise)
{
    if (from_account_number <= 0 || to_account_number <= 0 ||
        from_account_number == to_account_number || amount_paise <= 0 ||
        from_balance_paise == NULL || to_balance_paise == NULL)
        return BANKING_FAILED;

    if (pthread_mutex_lock(&banking_mutex) != 0)
        return BANKING_FAILED;

    int lock_fd = banking_lock_acquire(LOCK_FILE);
    if (lock_fd < 0) {
        pthread_mutex_unlock(&banking_mutex);
        return BANKING_FAILED;
    }

    BankAccount accounts[MAX_ACCOUNTS];
    size_t count = 0;
    int from_index = -1;
    int to_index = -1;
    int result = BANKING_FAILED;

    if (recovery_run(ACCOUNTS_FILE, "data/transactions.log") != 0)
        goto cleanup;

    if (storage_load_accounts(ACCOUNTS_FILE, accounts, MAX_ACCOUNTS, &count) != 0)
        goto cleanup;

    for (size_t i = 0; i < count; i++) {
        if (accounts[i].account_number == from_account_number)
            from_index = (int)i;
        if (accounts[i].account_number == to_account_number)
            to_index = (int)i;
    }

    if (from_index < 0 || to_index < 0 ||
        !accounts[from_index].active || !accounts[to_index].active)
        goto cleanup;

    long long old_from = accounts[from_index].balance_paise;
    long long old_to = accounts[to_index].balance_paise;

    if (amount_paise > old_from || amount_paise > LLONG_MAX - old_to)
        goto cleanup;

    long long new_from = old_from - amount_paise;
    long long new_to = old_to + amount_paise;

    char transaction_id[64];
    if (make_transaction_id(transaction_id, sizeof(transaction_id), "TRF") != 0)
        goto cleanup;

    /* Write-ahead journal: durable intent before changing account balances. */
    if (journal_prepare_transfer("data/transactions.log", transaction_id,
                                 from_account_number, old_from, new_from,
                                 to_account_number, old_to, new_to) != 0)
        goto cleanup;

    /* The operation is now journaled; failures below have an uncertain outcome. */
    result = BANKING_OUTCOME_UNKNOWN;
    accounts[from_index].balance_paise = new_from;
    accounts[to_index].balance_paise = new_to;

    /* One atomic replacement publishes both updated balances together. */
    if (storage_save_accounts(ACCOUNTS_FILE, accounts, count) != 0)
        goto cleanup;

    if (journal_commit("data/transactions.log", transaction_id) != 0) {
        /* The outcome is uncertain until startup recovery checks the journal. */
        result = BANKING_OUTCOME_UNKNOWN;
        goto cleanup;
    }

    *from_balance_paise = new_from;
    *to_balance_paise = new_to;
    result = BANKING_OK;

cleanup:
    banking_lock_release(lock_fd);
    pthread_mutex_unlock(&banking_mutex);
    return result;
}
