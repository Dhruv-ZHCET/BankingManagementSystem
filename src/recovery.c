#include "recovery.h"
#include "account.h"
#include "storage.h"
#include "journal.h"

#include <errno.h>
#include <stdio.h>

int recovery_run(const char *accounts_path,
                 const char *journal_path)
{
    BankAccount accounts[MAX_ACCOUNTS];
    size_t account_count = 0;

    JournalTransaction transactions[MAX_JOURNAL_TRANSACTIONS];
    size_t transaction_count = 0;

    /*
     * The caller must already hold accounts.lock.
     */

    if (storage_load_accounts(accounts_path,
                              accounts,
                              MAX_ACCOUNTS,
                              &account_count) != 0) {
        perror("Recovery: could not load accounts");
        return -1;
    }

    if (journal_load(journal_path,
                     transactions,
                     MAX_JOURNAL_TRANSACTIONS,
                     &transaction_count) != 0) {
        perror("Recovery: could not read journal");
        return -1;
    }

    for (size_t i = 0; i < transaction_count; i++) {
        JournalTransaction *tx = &transactions[i];

        if (tx->committed)
            continue;

        size_t first_index = account_count;
        size_t second_index = account_count;

        for (size_t j = 0; j < account_count; j++) {
            if (accounts[j].account_number == tx->account_number)
                first_index = j;
            if (tx->is_transfer &&
                accounts[j].account_number == tx->second_account_number)
                second_index = j;
        }

        if (first_index == account_count ||
            (tx->is_transfer && second_index == account_count)) {
            fprintf(stderr, "Recovery error: transaction %s references a missing account\n",
                    tx->transaction_id);
            errno = EINVAL;
            return -1;
        }

        BankAccount *first = &accounts[first_index];

        if (!tx->is_transfer) {
            if (first->balance_paise == tx->old_balance) {
                first->balance_paise = tx->new_balance;
                if (storage_save_accounts(accounts_path, accounts, account_count) != 0) {
                    perror("Recovery: could not save accounts");
                    return -1;
                }
            } else if (first->balance_paise != tx->new_balance) {
                fprintf(stderr,
                        "Recovery error: unexpected balance for account %d (transaction %s)\n",
                        tx->account_number, tx->transaction_id);
                errno = EINVAL;
                return -1;
            }
        } else {
            BankAccount *second = &accounts[second_index];
            int both_old = first->balance_paise == tx->old_balance &&
                           second->balance_paise == tx->second_old_balance;
            int both_new = first->balance_paise == tx->new_balance &&
                           second->balance_paise == tx->second_new_balance;

            if (both_old) {
                first->balance_paise = tx->new_balance;
                second->balance_paise = tx->second_new_balance;
                if (storage_save_accounts(accounts_path, accounts, account_count) != 0) {
                    perror("Recovery: could not save transfer balances");
                    return -1;
                }
            } else if (!both_new) {
                fprintf(stderr,
                        "Recovery error: inconsistent balances for transfer %s\n",
                        tx->transaction_id);
                errno = EINVAL;
                return -1;
            }
        }

        if (journal_commit(journal_path, tx->transaction_id) != 0) {
            perror("Recovery: could not commit transaction");
            return -1;
        }

        printf("Recovered transaction %s%s\n",
               tx->transaction_id, tx->is_transfer ? " (transfer)" : "");
    }

    return 0;
}