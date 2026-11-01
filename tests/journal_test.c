#include <stdio.h>

#include "journal.h"

int main(void)
{
    const char *path = "data/transactions.log";

    if (journal_prepare(path, "TXN001",
                        1001, 500000, 550000) != 0) {
        perror("Failed to prepare transaction");
        return 1;
    }

    if (journal_commit(path, "TXN001") != 0) {
        perror("Failed to commit transaction");
        return 1;
    }

    printf("Journal records written successfully.\n");

    return 0;
}