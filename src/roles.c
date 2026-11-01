#include "roles.h"
#include "role_modules.h"
#include <stddef.h>

int role_command_allowed(UserRole role, const char *command) {
    switch (role) {
        case ROLE_CUSTOMER: return customer_command_allowed(command);
        case ROLE_BANK_EMPLOYEE: return employee_command_allowed(command);
        case ROLE_MANAGER: return manager_command_allowed(command);
        case ROLE_ADMINISTRATOR: return administrator_command_allowed(command);
        default: return 0;
    }
}


int authorize_action(const BankingUser *user,
                     BankingAction action,
                     int target_account_number)
{
    if (user == 0)
        return 0;

    switch (user->role) {
        case ROLE_CUSTOMER:
            switch (action) {
                case ACTION_VIEW_BALANCE:
                case ACTION_DEPOSIT:
                case ACTION_WITHDRAW:
                case ACTION_TRANSFER:
                    return target_account_number ==
                           user->account_number;

                default:
                    return 0;
            }

        case ROLE_BANK_EMPLOYEE:
            switch (action) {
                case ACTION_VIEW_BALANCE:
                case ACTION_CREATE_ACCOUNT:
                case ACTION_PROCESS_LOAN:
                    return 1;

                default:
                    return 0;
            }

        case ROLE_MANAGER:
            switch (action) {
                case ACTION_VIEW_BALANCE:
                case ACTION_PROCESS_LOAN:
                case ACTION_APPROVE_LOAN:
                    return 1;

                default:
                    return 0;
            }

        case ROLE_ADMINISTRATOR:
            switch (action) {
                case ACTION_MANAGE_USERS:
                    return 1;

                default:
                    return 0;
            }
    }

    return 0;
}