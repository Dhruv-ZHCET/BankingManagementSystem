#ifndef RECOVERY_H
#define RECOVERY_H

/*
 * Recover interrupted transactions.
 *
 * Must be called while the caller holds the shared
 * banking lock.
 *
 * Returns 0 on success, -1 on failure.
 */
int recovery_run(const char *accounts_path,
                 const char *journal_path);

#endif