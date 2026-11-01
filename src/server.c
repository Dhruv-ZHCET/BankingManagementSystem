#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include "banking_service.h"
#include "auth.h"
#include "roles.h"
#include "recovery.h"
#include "locking.h"
#include "domain.h"
#include "role_modules.h"
#define SERVER_PORT 8080
#define USERS_FILE "data/users.txt"
static int receive_line(int fd, char *b, size_t c)
{
    size_t n = 0;
    if (c < 2)
        return -1;
    while (n < c - 1)
    {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r == 0)
            return n ? -1 : 0;
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ch == '\n')
        {
            b[n] = 0;
            return 1;
        }
        if (ch != '\r')
            b[n++] = ch;
    }
    return -1;
}
static int send_all(int fd, const char *m)
{
    size_t n = strlen(m), s = 0;
    while (s < n)
    {
        ssize_t w = send(fd, m + s, n - s, 0);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!w)
            return -1;
        s += (size_t)w;
    }
    return 0;
}
static const char *role_name(UserRole r)
{
    switch (r)
    {
    case ROLE_CUSTOMER:
        return "CUSTOMER";
    case ROLE_BANK_EMPLOYEE:
        return "EMPLOYEE";
    case ROLE_MANAGER:
        return "MANAGER";
    case ROLE_ADMINISTRATOR:
        return "ADMIN";
    }
    return "UNKNOWN";
}
static int parse_int(const char *s, int *out)
{
    if (!s || !*s)
        return -1;
    char *e;
    errno = 0;
    long v = strtol(s, &e, 10);
    if (errno || *e || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}
static int parse_ll(const char *s, long long *out)
{
    if (!s || !*s)
        return -1;
    char *e;
    errno = 0;
    long long v = strtoll(s, &e, 10);
    if (errno || *e)
        return -1;
    *out = v;
    return 0;
}
static int parse_role(const char *s, UserRole *r)
{
    if (!strcasecmp(s, "CUSTOMER"))
        *r = ROLE_CUSTOMER;
    else if (!strcasecmp(s, "EMPLOYEE"))
        *r = ROLE_BANK_EMPLOYEE;
    else if (!strcasecmp(s, "MANAGER"))
        *r = ROLE_MANAGER;
    else if (!strcasecmp(s, "ADMIN"))
        *r = ROLE_ADMINISTRATOR;
    else
        return -1;
    return 0;
}
static int customer_owns(const BankingUser *u, int account) { return u->role != ROLE_CUSTOMER || u->account_number == account; }
static int allowed_role(const BankingUser *u, const char *cmd) { return u && role_command_allowed(u->role, cmd); }

static int customer_add(const char *username, const char *password, const char *name, long long initial, char *out, size_t cap)
{
    int account = 0, user = 0;
    if (initial != 0)
        return -1;
    if (domain_open_account(name, 0, &account))
        return -1;
    if (auth_add_user(USERS_FILE, username, password, ROLE_CUSTOMER, account, &user))
    {
        (void)domain_set_account_active(account, 0);
        return -1;
    }
    snprintf(out, cap, "OK CUSTOMER_CREATED USER_ID=%d ACCOUNT=%d", user, account);
    return 0;
}
static void *handle_client(void *arg)
{
    int fd = *(int *)arg;
    free(arg);
    char line[1024], response[8192];
    AuthenticatedUser session = {0};
    int logged = 0;
    int rr = receive_line(fd, line, sizeof line);
    if (rr != 1)
    {
        send_all(fd, "ERR EXPECTED_LOGIN\n");
        close(fd);
        return NULL;
    }
    char cmd[16], user[32], pass[129], extra;
    int f = sscanf(line, "%15s %31s %128s %c", cmd, user, pass, &extra);
    if (f != 3 || strcmp(cmd, "LOGIN") || auth_login(USERS_FILE, user, pass, &session))
    {
        send_all(fd, "ERR LOGIN_FAILED\n");
        close(fd);
        return NULL;
    }
    logged = 1;
    snprintf(response, sizeof response, "OK LOGIN %s %d\n", role_name(session.user.role), session.user.account_number);
    if (send_all(fd, response))
        goto done;
    while ((rr = receive_line(fd, line, sizeof line)) == 1)
    {
        char copy[1024];
        snprintf(copy, sizeof copy, "%s", line);
        char *save = NULL;
        char *t[8] = {0};
        int nt = 0;
        for (char *p = strtok_r(copy, " \t", &save); p && nt < 8; p = strtok_r(NULL, " \t", &save))
            t[nt++] = p;
        if (!nt)
            snprintf(response, sizeof response, "ERR EMPTY_COMMAND");
        else if (!strcmp(t[0], "LOGOUT") || !strcmp(t[0], "EXIT"))
        {
            snprintf(response, sizeof response, "OK %s", t[0]);
            send_all(fd, strcat(response, "\n"));
            break;
        }
        else if (!allowed_role(&session.user, t[0]))
            snprintf(response, sizeof response, "ERR PERMISSION_DENIED");
        else
        {
            int a = 0, b = 0, active = 0;
            long long amount = 0, balance = 0, balance2 = 0;
            long id = 0;
            int rc = -1;
            response[0] = 0;
            if (!strcmp(t[0], "BALANCE") && nt == 2 && parse_int(t[1], &a) == 0 && customer_owns(&session.user, a) && banking_get_balance(a, &balance) == 0)
                snprintf(response, sizeof response, "OK BALANCE %lld", balance);
            else if (!strcmp(t[0], "DEPOSIT") && nt == 3 && parse_int(t[1], &a) == 0 && parse_ll(t[2], &amount) == 0 && customer_owns(&session.user, a))
            {
                rc = banking_deposit(a, amount, &balance);
                if (rc == 0)
                    snprintf(response, sizeof response, "OK DEPOSIT BALANCE=%lld", balance);
                else
                    snprintf(response, sizeof response, rc == BANKING_OUTCOME_UNKNOWN ? "ERR OUTCOME_UNKNOWN_RECOVERY_REQUIRED" : "ERR DEPOSIT_FAILED");
            }
            else if (!strcmp(t[0], "WITHDRAW") && nt == 3 && parse_int(t[1], &a) == 0 && parse_ll(t[2], &amount) == 0 && customer_owns(&session.user, a))
            {
                rc = banking_withdraw(a, amount, &balance);
                if (rc == 0)
                    snprintf(response, sizeof response, "OK WITHDRAW BALANCE=%lld", balance);
                else
                    snprintf(response, sizeof response, rc == BANKING_OUTCOME_UNKNOWN ? "ERR OUTCOME_UNKNOWN_RECOVERY_REQUIRED" : "ERR WITHDRAW_FAILED");
            }
            else if (!strcmp(t[0], "TRANSFER") && nt == 4 && parse_int(t[1], &a) == 0 && parse_int(t[2], &b) == 0 && parse_ll(t[3], &amount) == 0 && customer_owns(&session.user, a))
            {
                rc = banking_transfer(a, b, amount, &balance, &balance2);
                if (rc == 0)
                    snprintf(response, sizeof response, "OK TRANSFER SENDER_BALANCE=%lld RECEIVER_BALANCE=%lld", balance, balance2);
                else
                    snprintf(response, sizeof response, rc == BANKING_OUTCOME_UNKNOWN ? "ERR OUTCOME_UNKNOWN_RECOVERY_REQUIRED" : "ERR TRANSFER_FAILED");
            }
            else if (!strcmp(t[0], "HISTORY") && nt == 2 && parse_int(t[1], &a) == 0 && customer_owns(&session.user, a))
            {
                if (domain_history(a, response, sizeof response) == 0)
                {
                    if (!*response)
                        strcpy(response, "OK HISTORY EMPTY");
                    else
                    {
                        char tmp[8192];
                        snprintf(tmp, sizeof tmp, "OK HISTORY %s", response);
                        strcpy(response, tmp);
                    }
                }
                else
                    strcpy(response, "ERR HISTORY_FAILED");
            }
            else if (!strcmp(t[0], "ACCOUNTS") && nt == 1)
            {
                if (domain_list_accounts(response, sizeof response) == 0)
                {
                    char tmp[8192];
                    snprintf(tmp, sizeof tmp, "OK ACCOUNTS %s", response);
                    strcpy(response, tmp);
                }
                else
                    strcpy(response, "ERR ACCOUNTS_FAILED");
            }
            else if (!strcmp(t[0], "CUSTOMER_ADD") && nt == 5 && session.user.role == ROLE_BANK_EMPLOYEE && parse_ll(t[4], &amount) == 0)
            {
                if (customer_add(t[1], t[2], t[3], amount, response, sizeof response) != 0)
                    strcpy(response, "ERR CUSTOMER_CREATE_FAILED");
            }
            else if (!strcmp(t[0], "MODIFY_ACCOUNT") && nt == 3 && parse_int(t[1], &a) == 0)
            {
                if (domain_modify_account(a, t[2]) == 0)
                    strcpy(response, "OK ACCOUNT_MODIFIED");
                else
                    strcpy(response, "ERR MODIFY_FAILED");
            }
            else if (!strcmp(t[0], "CLOSE_ACCOUNT") && nt == 2 && parse_int(t[1], &a) == 0)
            {
                if (domain_set_account_active(a, 0) == 0)
                    strcpy(response, "OK ACCOUNT_CLOSED");
                else
                    strcpy(response, "ERR CLOSE_FAILED_BALANCE_MUST_BE_ZERO");
            }
            else if ((!strcmp(t[0], "ACTIVATE_ACCOUNT") || !strcmp(t[0], "DEACTIVATE_ACCOUNT")) && nt == 2 && parse_int(t[1], &a) == 0)
            {
                if (domain_set_account_active(a, !strcmp(t[0], "ACTIVATE_ACCOUNT")) == 0)
                    strcpy(response, "OK ACCOUNT_STATUS_UPDATED");
                else
                    strcpy(response, "ERR ACCOUNT_STATUS_UPDATE_FAILED");
            }
            else if (!strcmp(t[0], "LOAN_APPLY") && nt >= 4 && parse_int(t[1], &a) == 0 && parse_ll(t[2], &amount) == 0 && customer_owns(&session.user, a))
            {
                const char *p = strchr(line, ' ');
                p = p ? strchr(p + 1, ' ') : NULL;
                p = p ? strchr(p + 1, ' ') : NULL;
                if (p)
                {
                    while (*p == ' ')
                        p++;
                    if (*p && strlen(p) < 120 && !strchr(p, '|'))
                    {
                        if (domain_add_loan(a, amount, p, session.user.user_id, &id) == 0)
                            snprintf(response, sizeof response, "OK LOAN_APPLIED ID=%ld", id);
                        else
                            strcpy(response, "ERR LOAN_APPLICATION_FAILED");
                    }
                    else
                        strcpy(response, "ERR INVALID_PURPOSE");
                }
                else
                    strcpy(response, "ERR INVALID_PURPOSE");
            }
            else if (!strcmp(t[0], "LOANS") && (nt == 1 || nt == 2))
            {
                int filter = 0;
                if (session.user.role == ROLE_CUSTOMER)
                    filter = session.user.account_number;
                else if (nt == 2 && parse_int(t[1], &filter))
                    filter = -1;
                if (filter >= 0 && domain_list_loans(filter, response, sizeof response) == 0)
                {
                    char tmp[8192];
                    snprintf(tmp, sizeof tmp, "OK LOANS %s", *response ? response : "EMPTY");
                    strcpy(response, tmp);
                }
                else
                    strcpy(response, "ERR LOANS_FAILED");
            }
            else if (!strcmp(t[0], "LOAN_ASSIGN") && nt == 3 && parse_ll(t[1], &amount) == 0 && parse_int(t[2], &a) == 0)
            {
                BankingUser employee;
                id = (long)amount;
                if (auth_get_user_by_id(USERS_FILE, a, &employee) == 0 && employee.role == ROLE_BANK_EMPLOYEE && domain_assign_loan(id, a) == 0)
                    strcpy(response, "OK LOAN_ASSIGNED");
                else
                    strcpy(response, "ERR LOAN_ASSIGN_FAILED_EMPLOYEE_ID_REQUIRED");
            }
            else if (!strcmp(t[0], "LOAN_PROCESS") && nt == 3 && parse_ll(t[1], &amount) == 0)
            {
                id = (long)amount;
                if (domain_process_loan(id, t[2], session.user.user_id) == 0)
                    strcpy(response, "OK LOAN_PROCESSED");
                else
                    strcpy(response, "ERR LOAN_PROCESS_FAILED");
            }
            else if (!strcmp(t[0], "FEEDBACK") && nt >= 3 && parse_int(t[1], &a) == 0 && customer_owns(&session.user, a))
            {
                char message[180];
                char *p = strchr(line, ' ');
                p = p ? strchr(p + 1, ' ') : NULL;
                if (p)
                {
                    while (*p == ' ')
                        p++;
                    if (snprintf(message, sizeof message, "%s", p) < (int)sizeof message && domain_add_feedback(session.user.user_id, a, message, &id) == 0)
                        snprintf(response, sizeof response, "OK FEEDBACK_SUBMITTED ID=%ld", id);
                    else
                        strcpy(response, "ERR FEEDBACK_FAILED");
                }
                else
                    strcpy(response, "ERR INVALID_FEEDBACK");
            }
            else if (!strcmp(t[0], "FEEDBACK_LIST") && nt == 1)
            {
                if (domain_list_feedback(response, sizeof response) == 0)
                {
                    char tmp[8192];
                    snprintf(tmp, sizeof tmp, "OK FEEDBACK %s", *response ? response : "EMPTY");
                    strcpy(response, tmp);
                }
                else
                    strcpy(response, "ERR FEEDBACK_FAILED");
            }
            else if (!strcmp(t[0], "FEEDBACK_REVIEW") && nt == 2 && parse_ll(t[1], &amount) == 0)
            {
                if (domain_review_feedback((long)amount) == 0)
                    strcpy(response, "OK FEEDBACK_REVIEWED");
                else
                    strcpy(response, "ERR FEEDBACK_REVIEW_FAILED");
            }
            else if (!strcmp(t[0], "PASSWORD") && nt == 2)
            {
                if (auth_change_password(USERS_FILE, session.username, t[1]) == 0)
                    strcpy(response, "OK PASSWORD_CHANGED");
                else
                    strcpy(response, "ERR PASSWORD_CHANGE_FAILED");
            }
            else if (!strcmp(t[0], "USER_ADD") && nt == 5)
            {
                UserRole r;
                long long check_balance;
                if (parse_role(t[3], &r) || parse_int(t[4], &a) || a < 0)
                    strcpy(response, "ERR INVALID_USER_DATA");
                else if ((r == ROLE_CUSTOMER && (a <= 0 || banking_get_balance(a, &check_balance) != 0)) || (r != ROLE_CUSTOMER && a != 0))
                    strcpy(response, "ERR ACCOUNT_ROLE_MISMATCH");
                else if (auth_add_user(USERS_FILE, t[1], t[2], r, a, &b) == 0)
                    snprintf(response, sizeof response, "OK USER_CREATED ID=%d", b);
                else
                    strcpy(response, "ERR USER_CREATE_FAILED");
            }
            else if (!strcmp(t[0], "USER_ROLE") && nt == 3)
            {
                UserRole r;
                if (parse_role(t[2], &r))
                    strcpy(response, "ERR INVALID_ROLE");
                else if (auth_set_role(USERS_FILE, t[1], r) == 0)
                    strcpy(response, "OK ROLE_UPDATED");
                else
                    strcpy(response, "ERR ROLE_UPDATE_FAILED");
            }
            else if (!strcmp(t[0], "USER_ACTIVE") && nt == 3 && parse_int(t[2], &active) == 0)
            {
                if (auth_set_active(USERS_FILE, t[1], active) == 0)
                    strcpy(response, "OK USER_STATUS_UPDATED");
                else
                    strcpy(response, "ERR USER_STATUS_UPDATE_FAILED");
            }
            else if (!strcmp(t[0], "USER_LIST") && nt == 1)
            {
                if (auth_list_users(USERS_FILE, response, sizeof response) == 0)
                {
                    char tmp[8192];
                    snprintf(tmp, sizeof tmp, "OK USERS %s", response);
                    strcpy(response, tmp);
                }
                else
                    strcpy(response, "ERR USER_LIST_FAILED");
            }
            else
            {
                int target = 0;
                if (session.user.role == ROLE_CUSTOMER && nt >= 2 && parse_int(t[1], &target) == 0 && target != session.user.account_number && (!strcmp(t[0], "BALANCE") || !strcmp(t[0], "DEPOSIT") || !strcmp(t[0], "WITHDRAW") || !strcmp(t[0], "HISTORY") || !strcmp(t[0], "FEEDBACK") || !strcmp(t[0], "LOAN_APPLY")))
                    strcpy(response, "ERR PERMISSION_DENIED");
                else
                    strcpy(response, "ERR INVALID_REQUEST_OR_ARGUMENTS");
            }
        }
        if (send_all(fd, response) || send_all(fd, "\n"))
            break;
    }
done:
    if (logged)
        auth_logout(session.username);
    close(fd);
    return NULL;
}
int main(void)
{
    int port = SERVER_PORT;
    const char *port_env = getenv("BANK_PORT");
    if (port_env && *port_env)
    {
        char *end = NULL;
        long v = strtol(port_env, &end, 10);
        if (!end || *end || v < 1024 || v > 65535)
        {
            fprintf(stderr, "Invalid BANK_PORT\n");
            return 1;
        }
        port = (int)v;
    }
    if (auth_init(USERS_FILE))
    {
        fprintf(stderr, "Could not initialize user database\n");
        return 1;
    }
    int lock = banking_lock_acquire("data/accounts.lock");
    if (lock < 0)
        return 1;
    int rr = recovery_run("data/accounts.txt", "data/transactions.log");
    banking_lock_release(lock);
    if (rr)
    {
        fprintf(stderr, "Recovery failed; server not started\n");
        return 1;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("socket");
        return 1;
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 32) < 0)
    {
        perror("bind/listen");
        close(s);
        return 1;
    }
    printf("Bank server listening on 127.0.0.1:%d\n", port);
    for (;;)
    {
        int *c = malloc(sizeof *c);
        if (!c)
            continue;
        *c = accept(s, NULL, NULL);
        if (*c < 0)
        {
            free(c);
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }
        pthread_t th;
        if (pthread_create(&th, NULL, handle_client, c))
        {
            close(*c);
            free(c);
        }
        else
            pthread_detach(th);
    }
    close(s);
    return 0;
}
