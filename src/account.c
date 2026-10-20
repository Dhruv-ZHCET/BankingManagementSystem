#include "account.h"

#include <limits.h>
#include <string.h>

bool account_init(BankAccount *account,
                  int account_number,
                  const char *customer_name,
                  long long initial_balance_paise)
{
    if (account == NULL || customer_name == NULL)
        return false;

    if (account_number <= 0 || initial_balance_paise < 0)
        return false;

    size_t length = strlen(customer_name);

    if (length == 0 || length >= sizeof(account->customer_name))
        return false;

    account->account_number = account_number;
    strcpy(account->customer_name, customer_name);
    account->balance_paise = initial_balance_paise;
    account->active = true;

    return true;
}

bool account_deposit(BankAccount *account, long long amount_paise)
{
    if (account == NULL || !account->active || amount_paise <= 0)
        return false;

    if (account->balance_paise > LLONG_MAX - amount_paise)
        return false;

    account->balance_paise += amount_paise;
    return true;
}

bool account_withdraw(BankAccount *account, long long amount_paise)
{
    if (account == NULL || !account->active || amount_paise <= 0)
        return false;

    if (amount_paise > account->balance_paise)
        return false;

    account->balance_paise -= amount_paise;
    return true;
}

long long account_get_balance(const BankAccount *account)
{
    if (account == NULL)
        return -1;

    return account->balance_paise;
}