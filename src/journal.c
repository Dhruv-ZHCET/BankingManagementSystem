#include "journal.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(fd,
                               buffer + written,
                               length - written);

        if (result < 0)
            return -1;

        if (result == 0)
            return -1;

        written += (size_t)result;
    }

    return 0;
}

static int append_record(const char *path, const char *record)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);

    if (fd == -1)
        return -1;

    size_t length = strlen(record);
    int failed = 0;

    if (write_all(fd, record, length) != 0)
        failed = 1;

    /*
     * Make the journal data durable before returning success.
     */
    if (!failed && fsync(fd) == -1)
        failed = 1;

    if (close(fd) == -1)
        failed = 1;

    return failed ? -1 : 0;
}

int journal_prepare(const char *path,
                          const char *transaction_id,
                          int account_number,
                          long long old_balance,
                          long long new_balance)
{
    if (path == NULL || transaction_id == NULL ||
        transaction_id[0] == '\0' ||
        strchr(transaction_id, '|') != NULL ||
        strchr(transaction_id, '\n') != NULL ||
        account_number <= 0 ||
        old_balance < 0 ||
        new_balance < 0) {
        return -1;
    }

    char record[256];

    int length = snprintf(record, sizeof(record),
                          "%s|PREPARED|%d|%lld|%lld\n",
                          transaction_id,
                          account_number,
                          old_balance,
                          new_balance);

    if (length < 0 || (size_t)length >= sizeof(record))
        return -1;

    if (append_record(path, record) != 0)
        return -1;

    return 0;
}

int journal_prepare_transfer(const char *path,
                             const char *transaction_id,
                             int from_account,
                             long long old_from,
                             long long new_from,
                             int to_account,
                             long long old_to,
                             long long new_to)
{
    if (path == NULL || transaction_id == NULL ||
        transaction_id[0] == '\0' ||
        strchr(transaction_id, '|') != NULL ||
        strchr(transaction_id, '\n') != NULL ||
        from_account <= 0 || to_account <= 0 ||
        from_account == to_account ||
        old_from < 0 || new_from < 0 ||
        old_to < 0 || new_to < 0) {
        return -1;
    }

    char record[320];
    int length = snprintf(record, sizeof(record),
                          "%s|TRANSFER_PREPARED|%d|%lld|%lld|%d|%lld|%lld\n",
                          transaction_id, from_account, old_from, new_from,
                          to_account, old_to, new_to);

    if (length < 0 || (size_t)length >= sizeof(record))
        return -1;

    return append_record(path, record);
}

int journal_commit(const char *path,
                   const char *transaction_id)
{
    if (path == NULL || transaction_id == NULL ||
        transaction_id[0] == '\0' ||
        strchr(transaction_id, '|') != NULL ||
        strchr(transaction_id, '\n') != NULL) {
        return -1;
    }

    char record[128];

    int length = snprintf(record, sizeof(record),
                          "%s|COMMITTED\n",
                          transaction_id);

    if (length < 0 || (size_t)length >= sizeof(record))
        return -1;

    return append_record(path, record);
}

static int find_transaction(
    const JournalTransaction transactions[],
    size_t count,
    const char *id)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(transactions[i].transaction_id, id) == 0) {
            return (int)i;
        }
    }

    return -1;
}

int journal_load(const char *path,
                 JournalTransaction transactions[],
                 size_t capacity,
                 size_t *count)
{
    if (path == NULL || transactions == NULL || count == NULL) {
        errno = EINVAL;
        return -1;
    }

    *count = 0;

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        if (errno == ENOENT) {
            return 0;  // No journal yet: nothing to recover.
        }

        return -1;
    }

    char line[256];
    size_t total = 0;
    int failed = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);

        /* Reject lines that do not fit in our buffer. */
        if (length == 0 || line[length - 1] != '\n') {
            failed = 1;
            break;
        }

        line[length - 1] = '\0';

        char id[MAX_TRANSACTION_ID];
        int account_number;
        long long old_balance, new_balance;
        char extra;

        /*
         * Transfer record:
         * ID|TRANSFER_PREPARED|from|old_from|new_from|to|old_to|new_to
        */
        int second_account_number;
        long long second_old_balance, second_new_balance;
        int fields = sscanf(
            line,
            "%63[^|]|TRANSFER_PREPARED|%d|%lld|%lld|%d|%lld|%lld%c",
            id,
            &account_number,
            &old_balance,
            &new_balance,
            &second_account_number,
            &second_old_balance,
            &second_new_balance,
            &extra
        );

        if (fields == 7) {
            if (id[0] == '\0' || account_number <= 0 ||
                second_account_number <= 0 ||
                account_number == second_account_number ||
                old_balance < 0 || new_balance < 0 ||
                second_old_balance < 0 || second_new_balance < 0 ||
                find_transaction(transactions, total, id) != -1 ||
                total >= capacity) {
                failed = 1;
                break;
            }

            JournalTransaction *tx = &transactions[total];
            memset(tx, 0, sizeof(*tx));
            snprintf(tx->transaction_id, sizeof(tx->transaction_id), "%s", id);
            tx->account_number = account_number;
            tx->old_balance = old_balance;
            tx->new_balance = new_balance;
            tx->second_account_number = second_account_number;
            tx->second_old_balance = second_old_balance;
            tx->second_new_balance = second_new_balance;
            tx->is_transfer = 1;
            tx->committed = 0;
            total++;
            continue;
        }

        /*
         * PREPARED record:
         * ID|PREPARED|account|old_balance|new_balance
         */
        fields = sscanf(
            line,
            "%63[^|]|PREPARED|%d|%lld|%lld%c",
            id,
            &account_number,
            &old_balance,
            &new_balance,
            &extra
        );

        if (fields == 4) {
            if (id[0] == '\0' ||
                account_number <= 0 ||
                old_balance < 0 ||
                new_balance < 0 ||
                find_transaction(transactions, total, id) != -1 ||
                total >= capacity) {
                failed = 1;
                break;
            }

            JournalTransaction *tx = &transactions[total];
            memset(tx, 0, sizeof(*tx));

            snprintf(tx->transaction_id,
                     sizeof(tx->transaction_id), "%s", id);

            tx->account_number = account_number;
            tx->old_balance = old_balance;
            tx->new_balance = new_balance;
            tx->committed = 0;

            total++;
            continue;
        }

        /*
         * COMMITTED record:
         * ID|COMMITTED
         */
        fields = sscanf(line, "%63[^|]|COMMITTED%c", id, &extra);

        if (fields == 1) {
            int index = find_transaction(transactions, total, id);

            if (id[0] == '\0' ||
                index == -1 ||
                transactions[index].committed) {
                failed = 1;
                break;
            }

            transactions[index].committed = 1;
            continue;
        }

        /* Unknown or malformed record. */
        failed = 1;
        break;
    }

    if (ferror(file)) {
        failed = 1;
    }

    if (fclose(file) == EOF) {
        failed = 1;
    }

    if (failed) {
        errno = EINVAL;
        return -1;
    }

    *count = total;
    return 0;
}