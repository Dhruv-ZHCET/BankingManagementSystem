#define _POSIX_C_SOURCE 200809L
#include "domain.h"
#include "storage.h"
#include "locking.h"
#include "journal.h"
#include "recovery.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#define ACCOUNTS "data/accounts.txt"
#define LOCKFILE "data/accounts.lock"
#define LOANS "data/loans.txt"
#define FEEDBACK "data/feedback.txt"
static int append_durable(const char *path, const char *line)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return -1;
    size_t n = strlen(line), done = 0;
    while (done < n)
    {
        ssize_t w = write(fd, line + done, n - done);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (w == 0)
        {
            close(fd);
            return -1;
        }
        done += (size_t)w;
    }
    int rc = fsync(fd);
    if (close(fd) < 0)
        rc = -1;
    return rc;
}
static int safe_text(const char *s, size_t max)
{
    if (!s || !*s || strlen(s) >= max || strchr(s, '|') || strchr(s, '\n') || strchr(s, '\r'))
        return 0;
    return 1;
}
static int next_id(const char *path, const char *kind, long *id)
{
    FILE *f = fopen(path, "r");
    long max = 0;
    if (f)
    {
        char line[512];
        while (fgets(line, sizeof line, f))
        {
            char *e;
            long x = strtol(line, &e, 10);
            if (e != line && x > max)
                max = x;
        }
        fclose(f);
    }
    if (max == LONG_MAX)
        return -1;
    *id = max + 1;
    (void)kind;
    return 0;
}
int domain_open_account(const char *name, long long initial, int *account_number)
{
    if (!safe_text(name, 50) || initial < 0 || !account_number)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    int rc = -1;
    BankAccount a[MAX_ACCOUNTS];
    size_t n = 0;
    if (recovery_run(ACCOUNTS, "data/transactions.log") || storage_load_accounts(ACCOUNTS, a, MAX_ACCOUNTS, &n) || n >= MAX_ACCOUNTS)
        goto done;
    int max = 1000;
    for (size_t i = 0; i < n; i++)
        if (a[i].account_number > max)
            max = a[i].account_number;
    if (max == INT_MAX)
        goto done;
    if (!account_init(&a[n], max + 1, name, initial))
        goto done;
    a[n].active = true;
    if (storage_save_accounts(ACCOUNTS, a, n + 1))
        goto done;
    *account_number = max + 1;
    rc = 0;
done:
    banking_lock_release(l);
    return rc;
}
int domain_modify_account(int no, const char *name)
{
    if (no <= 0 || !safe_text(name, 50))
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    BankAccount a[MAX_ACCOUNTS];
    size_t n = 0;
    int rc = -1;
    if (!recovery_run(ACCOUNTS, "data/transactions.log") && !storage_load_accounts(ACCOUNTS, a, MAX_ACCOUNTS, &n))
    {
        for (size_t i = 0; i < n; i++)
            if (a[i].account_number == no)
            {
                strcpy(a[i].customer_name, name);
                rc = storage_save_accounts(ACCOUNTS, a, n);
                break;
            }
    }
    banking_lock_release(l);
    return rc;
}
int domain_set_account_active(int no, int active)
{
    if (no <= 0 || (active != 0 && active != 1))
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    BankAccount a[MAX_ACCOUNTS];
    size_t n = 0;
    int rc = -1;
    if (!recovery_run(ACCOUNTS, "data/transactions.log") && !storage_load_accounts(ACCOUNTS, a, MAX_ACCOUNTS, &n))
    {
        for (size_t i = 0; i < n; i++)
            if (a[i].account_number == no)
            {
                if (!active && a[i].balance_paise != 0)
                    goto done;
                a[i].active = active != 0;
                rc = storage_save_accounts(ACCOUNTS, a, n);
                break;
            }
    }
done:
    banking_lock_release(l);
    return rc;
}
int domain_list_accounts(char *out, size_t cap)
{
    if (!out || cap < 2)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    BankAccount a[MAX_ACCOUNTS];
    size_t n = 0;
    int rc = -1;
    out[0] = 0;
    if (!storage_load_accounts(ACCOUNTS, a, MAX_ACCOUNTS, &n))
    {
        size_t used = 0;
        for (size_t i = 0; i < n; i++)
        {
            int k = snprintf(out + used, cap - used, "%s%d|%s|%lld|%s", i ? ";" : "", a[i].account_number, a[i].customer_name, a[i].balance_paise, a[i].active ? "ACTIVE" : "INACTIVE");
            if (k < 0 || (size_t)k >= cap - used)
            {
                rc = -1;
                goto done;
            }
            used += (size_t)k;
        }
        rc = 0;
    }
done:
    banking_lock_release(l);
    return rc;
}
static int load_loans(LoanRecord *a, size_t cap, size_t *n)
{
    *n = 0;
    FILE *f = fopen(LOANS, "r");
    if (!f)
        return errno == ENOENT ? 0 : -1;
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        LoanRecord r = {0};
        int fields = sscanf(line, "%ld|%d|%lld|%119[^|]|%15[^|]|%d|%d", &r.id, &r.account_number, &r.amount_paise, r.purpose, r.status, &r.assigned_employee, &r.created_by);
        if (fields != 7 || *n >= cap)
        {
            fclose(f);
            return -1;
        }
        a[(*n)++] = r;
    }
    fclose(f);
    return 0;
}
static int save_loans(const LoanRecord *a, size_t n)
{
    char tmp[] = "data/loans.txt.tmp";
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    FILE *f = fdopen(fd, "w");
    if (!f)
    {
        close(fd);
        unlink(tmp);
        return -1;
    }
    int rc = 0;
    for (size_t i = 0; i < n; i++)
        if (fprintf(f, "%ld|%d|%lld|%s|%s|%d|%d\n", a[i].id, a[i].account_number, a[i].amount_paise, a[i].purpose, a[i].status, a[i].assigned_employee, a[i].created_by) < 0)
            rc = -1;
    if (fflush(f) || fsync(fd))
        rc = -1;
    if (fclose(f))
        rc = -1;
    if (rc || rename(tmp, LOANS))
    {
        unlink(tmp);
        return -1;
    }
    return 0;
}
int domain_add_loan(int account, long long amount, const char *purpose, int user, long *id)
{
    if (account <= 0 || amount <= 0 || !safe_text(purpose, 120) || !id)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    int rc = -1;
    BankAccount ac[MAX_ACCOUNTS];
    size_t an = 0;
    LoanRecord loans[MAX_LOANS];
    size_t n = 0;
    if (storage_load_accounts(ACCOUNTS, ac, MAX_ACCOUNTS, &an) || load_loans(loans, MAX_LOANS, &n) || n >= MAX_LOANS)
        goto done;
    int exists = 0;
    for (size_t i = 0; i < an; i++)
        if (ac[i].account_number == account && ac[i].active)
            exists = 1;
    if (!exists)
        goto done;
    long nid;
    if (next_id(LOANS, "loan", &nid))
        goto done;
    loans[n] = (LoanRecord){.id = nid, .account_number = account, .amount_paise = amount, .assigned_employee = 0, .created_by = user};
    strcpy(loans[n].purpose, purpose);
    strcpy(loans[n].status, "PENDING");
    if (save_loans(loans, n + 1))
        goto done;
    *id = nid;
    rc = 0;
done:
    banking_lock_release(l);
    return rc;
}
int domain_list_loans(int filter, char *out, size_t cap)
{
    if (!out || cap < 2)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    LoanRecord a[MAX_LOANS];
    size_t n = 0;
    int rc = -1;
    out[0] = 0;
    if (!load_loans(a, MAX_LOANS, &n))
    {
        size_t u = 0;
        for (size_t i = 0; i < n; i++)
        {
            if (filter > 0 && a[i].account_number != filter)
                continue;
            int k = snprintf(out + u, cap - u, "%s%ld|%d|%lld|%s|%s|%d", u ? ";" : "", a[i].id, a[i].account_number, a[i].amount_paise, a[i].purpose, a[i].status, a[i].assigned_employee);
            if (k < 0 || (size_t)k >= cap - u)
            {
                rc = -1;
                goto done;
            }
            u += (size_t)k;
        }
        rc = 0;
    }
done:
    banking_lock_release(l);
    return rc;
}
int domain_assign_loan(long id, int employee)
{
    if (id <= 0 || employee <= 0)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    LoanRecord a[MAX_LOANS];
    size_t n = 0;
    int rc = -1;
    if (!load_loans(a, MAX_LOANS, &n))
    {
        for (size_t i = 0; i < n; i++)
            if (a[i].id == id && strcmp(a[i].status, "PENDING") == 0)
            {
                a[i].assigned_employee = employee;
                strcpy(a[i].status, "ASSIGNED");
                rc = save_loans(a, n);
                break;
            }
    }
    banking_lock_release(l);
    return rc;
}
int domain_process_loan(long id, const char *decision, int employee)
{
    if (id <= 0 || !decision || (strcmp(decision, "APPROVE") && strcmp(decision, "REJECT")))
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    LoanRecord a[MAX_LOANS];
    size_t n = 0;
    int rc = -1;
    if (!load_loans(a, MAX_LOANS, &n))
    {
        for (size_t i = 0; i < n; i++)
            if (a[i].id == id && (strcmp(a[i].status, "PENDING") == 0 || strcmp(a[i].status, "ASSIGNED") == 0) && (a[i].assigned_employee == employee))
            {
                strcpy(a[i].status, strcmp(decision, "APPROVE") == 0 ? "APPROVED" : "REJECTED");
                a[i].assigned_employee = employee;
                rc = save_loans(a, n);
                break;
            }
    }
    banking_lock_release(l);
    return rc;
}
int domain_add_feedback(int user, int account, const char *message, long *id)
{
    if (user <= 0 || account <= 0 || !safe_text(message, 180) || !id)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    int rc = -1;
    long nid;
    if (next_id(FEEDBACK, "feedback", &nid))
        goto done;
    char line[300];
    int n = snprintf(line, sizeof line, "%ld|%d|%d|%s|OPEN\n", nid, user, account, message);
    if (n < 0 || (size_t)n >= sizeof line || append_durable(FEEDBACK, line))
        goto done;
    *id = nid;
    rc = 0;
done:
    banking_lock_release(l);
    return rc;
}
int domain_list_feedback(char *out, size_t cap)
{
    if (!out || cap < 2)
        return -1;
    int l = banking_lock_acquire(LOCKFILE);
    if (l < 0)
        return -1;
    FILE *f = fopen(FEEDBACK, "r");
    out[0] = 0;
    if (!f)
    {
        banking_lock_release(l);
        return errno == ENOENT ? 0 : -1;
    }
    size_t u = 0;
    char line[300];
    int rc = 0;
    while (fgets(line, sizeof line, f))
    {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (u + n + 2 >= cap)
        {
            rc = -1;
            break;
        }
        if (u)
            out[u++] = ';';
        memcpy(out + u, line, n + 1);
        u += n;
    }
    fclose(f);
    banking_lock_release(l);
    return rc;
}
int domain_history(int account, char *out, size_t cap)
{
    if (account <= 0 || !out || cap < 2)
        return -1;
    int lock = banking_lock_acquire(LOCKFILE);
    if (lock < 0)
        return -1;
    JournalTransaction t[MAX_JOURNAL_TRANSACTIONS];
    size_t n = 0;
    if (journal_load("data/transactions.log", t, MAX_JOURNAL_TRANSACTIONS, &n))
    {
        banking_lock_release(lock);
        return -1;
    }
    out[0] = 0;
    size_t u = 0;
    int rc = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (t[i].account_number != account && t[i].second_account_number != account)
            continue;
        int k;
        if (t[i].is_transfer)
            k = snprintf(out + u, cap - u, "%s%s|TRANSFER|%d|%lld|%lld|%d|%lld|%lld|%s", u ? ";" : "", t[i].transaction_id, t[i].account_number, t[i].old_balance, t[i].new_balance, t[i].second_account_number, t[i].second_old_balance, t[i].second_new_balance, t[i].committed ? "COMMITTED" : "PENDING");
        else
            k = snprintf(out + u, cap - u, "%s%s|BALANCE_CHANGE|%d|%lld|%lld|%s", u ? ";" : "", t[i].transaction_id, t[i].account_number, t[i].old_balance, t[i].new_balance, t[i].committed ? "COMMITTED" : "PENDING");
        if (k < 0 || (size_t)k >= cap - u)
        {
            rc = -1;
            break;
        }
        u += (size_t)k;
    }
    banking_lock_release(lock);
    return rc;
}

int domain_review_feedback(long id)
{
    if (id <= 0)
        return -1;
    int lock = banking_lock_acquire(LOCKFILE);
    if (lock < 0)
        return -1;
    FILE *in = fopen(FEEDBACK, "r");
    if (!in)
    {
        banking_lock_release(lock);
        return -1;
    }
    char temp[] = "data/feedback.txt.tmp";
    FILE *out = fopen(temp, "w");
    if (!out)
    {
        fclose(in);
        banking_lock_release(lock);
        return -1;
    }
    int found = 0, rc = 0;
    char line[300];
    while (fgets(line, sizeof line, in))
    {
        long fid;
        int uid, account;
        char message[180], status[16];
        if (sscanf(line, "%ld|%d|%d|%179[^|]|%15[^\n]", &fid, &uid, &account, message, status) != 5)
        {
            rc = -1;
            break;
        }
        if (fid == id)
        {
            found = 1;
            strcpy(status, "REVIEWED");
        }
        if (fprintf(out, "%ld|%d|%d|%s|%s\n", fid, uid, account, message, status) < 0)
        {
            rc = -1;
            break;
        }
    }
    if (ferror(in) || fflush(out) != 0 || fsync(fileno(out)) != 0)
        rc = -1;
    if (fclose(in) != 0)
        rc = -1;
    if (fclose(out) != 0)
        rc = -1;
    if (!found)
        rc = -1;
    if (rc == 0 && rename(temp, FEEDBACK) != 0)
        rc = -1;
    if (rc != 0)
        unlink(temp);
    banking_lock_release(lock);
    return rc;
}
