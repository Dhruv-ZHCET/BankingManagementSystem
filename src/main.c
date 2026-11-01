
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recovery.h"
#include "banking_service.h"


#include "account.h"
#include "storage.h"
#include "locking.h"

#define ACCOUNTS_FILE "data/accounts.txt"
#define LOCK_FILE     "data/accounts.lock"
#define LOG_FILE      "data/transactions.log"


static int run_startup_recovery(void)
{
    int lock_fd = banking_lock_acquire(LOCK_FILE);

    if (lock_fd == -1) {
        perror("Could not acquire banking lock");
        return -1;
    }

    printf("Checking for interrupted transactions...\n");

    int result = recovery_run(
        ACCOUNTS_FILE,
        LOG_FILE
    );

    if (result == -1) {
        fprintf(stderr,
                "Startup recovery failed. "
                "Banking operations are disabled.\n");
    } else {
        printf("Startup recovery completed.\n");
    }

    if (banking_lock_release(lock_fd) == -1) {
        perror("Could not release banking lock");
        return -1;
    }

    return result;
}

static int parse_number(const char *text, long long *value)
{
    char *end = NULL;

    errno = 0;
    long long result = strtoll(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0')
        return -1;

    *value = result;
    return 0;
}

static int run_transaction(int account_number,
                           long long amount_paise,
                           int is_deposit)
{
    long long new_balance;

    int result;

    if (is_deposit) {
        result = banking_deposit(
            account_number,
            amount_paise,
            &new_balance
        );
    } else {
        result = banking_withdraw(
            account_number,
            amount_paise,
            &new_balance
        );
    }

    if (result != 0) {
        fprintf(stderr, "Transaction failed.\n");
        return -1;
    }

    printf("Transaction successful. New balance: %lld paise\n",
           new_balance);

    return 0;
}

static int list_accounts(void)
{
    int lock_fd = banking_lock_acquire(LOCK_FILE);

    if (lock_fd == -1) {
        perror("Could not acquire banking lock");
        return -1;
    }

    BankAccount accounts[MAX_ACCOUNTS];
    size_t count = 0;
    int status = -1;

    if (storage_load_accounts(ACCOUNTS_FILE, accounts,
                              MAX_ACCOUNTS, &count) != 0) {
        fprintf(stderr, "Could not load accounts.\n");
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("Account: %d | Name: %s | Balance: Rs. %lld.%02lld | %s\n",
                   accounts[i].account_number,
                   accounts[i].customer_name,
                   accounts[i].balance_paise / 100,
                   accounts[i].balance_paise % 100,
                   accounts[i].active ? "Active" : "Inactive");
        }

        printf("Total accounts: %zu\n", count);
        status = 0;
    }

    if (banking_lock_release(lock_fd) != 0) {
        perror("Could not release banking lock");
        return -1;
    }

    return status;
}


int main(int argc, char *argv[])
{
    if (run_startup_recovery() != 0) {
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "list") == 0)
        return list_accounts() == 0 ? 0 : 1;

    if (argc == 4 &&
        (strcmp(argv[1], "deposit") == 0 ||
         strcmp(argv[1], "withdraw") == 0)) {

        long long account_value;
        long long amount_paise;

        if (parse_number(argv[2], &account_value) != 0 ||
            parse_number(argv[3], &amount_paise) != 0 ||
            account_value < 1 || account_value > INT_MAX ||
            amount_paise <= 0) {
            fprintf(stderr,
                    "Invalid account number or amount in paise.\n");
            return 1;
        }

        int is_deposit = strcmp(argv[1], "deposit") == 0;

        return run_transaction((int)account_value,
                               amount_paise,
                               is_deposit) == 0 ? 0 : 1;
    }

    fprintf(stderr,
            "Usage:\n"
            "  %s list\n"
            "  %s deposit ACCOUNT_NUMBER AMOUNT_PAISE\n"
            "  %s withdraw ACCOUNT_NUMBER AMOUNT_PAISE\n",
            argv[0], argv[0], argv[0]);

    return 1;
}
