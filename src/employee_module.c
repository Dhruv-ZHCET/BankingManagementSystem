#include <string.h>
#include "role_modules.h"
int employee_command_allowed(const char *c) {
    if (!c) return 0;
    return !strcmp(c,"BALANCE") || !strcmp(c,"HISTORY") ||
           !strcmp(c,"LOANS") || !strcmp(c,"CUSTOMER_ADD") ||
           !strcmp(c,"MODIFY_ACCOUNT") || !strcmp(c,"CLOSE_ACCOUNT") ||
           !strcmp(c,"LOAN_PROCESS") || !strcmp(c,"PASSWORD");
}
