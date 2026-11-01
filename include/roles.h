#ifndef ROLES_H
#define ROLES_H

typedef enum {
    ROLE_CUSTOMER,
    ROLE_BANK_EMPLOYEE,
    ROLE_MANAGER,
    ROLE_ADMINISTRATOR
} UserRole;

typedef enum {
    ACTION_VIEW_BALANCE,
    ACTION_DEPOSIT,
    ACTION_WITHDRAW,
    ACTION_TRANSFER,
    ACTION_CREATE_ACCOUNT,
    ACTION_PROCESS_LOAN,
    ACTION_APPROVE_LOAN,
    ACTION_MANAGE_USERS
} BankingAction;

typedef struct {
    int user_id;
    UserRole role;
    int account_number;
} BankingUser;

int role_command_allowed(UserRole role, const char *command);

int authorize_action(const BankingUser *user,
                    BankingAction action,
                    int target_account_number);

#endif