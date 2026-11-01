#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdbool.h>

typedef struct {
    int account_number;
    char customer_name[50];
    long long balance_paise;
    bool active;
} BankAccount;

bool account_init(BankAccount *account,
                  int account_number,
                  const char *customer_name,
                  long long initial_balance_paise);

bool account_deposit(BankAccount *account, long long amount_paise);

bool account_withdraw(BankAccount *account, long long amount_paise);

long long account_get_balance(const BankAccount *account);

#endif