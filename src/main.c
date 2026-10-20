#include <stdio.h>
#include <string.h>

#include "account.h"
#include "storage.h"

int main(int argc, char *argv[])
{
    const char *path = "data/account.txt";

    if (argc != 2) {
        printf("Usage: %s save|load\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "save") == 0) {
        BankAccount account;

        if (!account_init(&account, 1002, "Dhruv", 500000)) {
            printf("Account initialization failed.\n");
            return 1;
        }

        if (!account_deposit(&account, 10000)) {
            printf("Deposit failed.\n");
            return 1;
        }

        if (storage_save_account(path, &account) != 0) {
            perror("Saving account failed");
            return 1;
        }

        printf("Account saved successfully.\n");
    }
    else if (strcmp(argv[1], "load") == 0) {
        BankAccount account;

        if (storage_load_account(path, &account) != 0) {
            printf("Could not load account. Does the file exist?\n");
            return 1;
        }

        printf("Account number: %d\n", account.account_number);
        printf("Customer name: %s\n", account.customer_name);
        printf("Balance: Rs. %lld.%02lld\n",
               account.balance_paise / 100,
               account.balance_paise % 100);
        printf("Active: %s\n", account.active ? "Yes" : "No");
    }
    else {
        printf("Invalid command. Use save or load.\n");
        return 1;
    }

    return 0;
}