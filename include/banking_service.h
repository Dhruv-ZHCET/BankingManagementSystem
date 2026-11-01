#ifndef BANKING_SERVICE_H
#define BANKING_SERVICE_H

typedef enum {
    BANKING_OK = 0,
    BANKING_FAILED = -1,
    BANKING_OUTCOME_UNKNOWN = -2
} BankingResult;

int banking_get_balance(int account_number,
                        long long *balance_paise);

int banking_deposit(int account_number,
                    long long amount_paise,
                    long long *new_balance_paise);

int banking_withdraw(int account_number,
                     long long amount_paise,
                     long long *new_balance_paise);

int banking_transfer(int from_account_number,
                     int to_account_number,
                     long long amount_paise,
                     long long *from_balance_paise,
                     long long *to_balance_paise);

#endif