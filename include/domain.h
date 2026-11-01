#ifndef DOMAIN_H
#define DOMAIN_H
#include <stddef.h>
#include "account.h"
#define MAX_LOANS 500
#define MAX_FEEDBACK 500

typedef struct
{
    long id;
    int account_number;
    long long amount_paise;
    char purpose[120];
    char status[16];
    int assigned_employee;
    int created_by;
} LoanRecord;
typedef struct
{
    long id;
    int user_id;
    int account_number;
    char message[180];
    char status[16];
} FeedbackRecord;

int domain_open_account(const char *name, long long initial_paise, int *account_number);
int domain_modify_account(int account_number, const char *name);
int domain_set_account_active(int account_number, int active);
int domain_list_accounts(char *out, size_t cap);
int domain_add_loan(int account_number, long long amount, const char *purpose, int user_id, long *loan_id);
int domain_list_loans(int account_number_filter, char *out, size_t cap);
int domain_assign_loan(long id, int employee_id);
int domain_process_loan(long id, const char *decision, int employee_id);
int domain_add_feedback(int user_id, int account_number, const char *message, long *feedback_id);
int domain_list_feedback(char *out, size_t cap);
int domain_review_feedback(long id);
int domain_history(int account_number, char *out, size_t cap);
#endif
