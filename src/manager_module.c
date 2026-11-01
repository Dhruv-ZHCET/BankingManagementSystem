#include <string.h>
#include "role_modules.h"
int manager_command_allowed(const char *c) {
    if (!c) return 0;
    return !strcmp(c,"ACTIVATE_ACCOUNT") || !strcmp(c,"DEACTIVATE_ACCOUNT") ||
           !strcmp(c,"LOAN_ASSIGN") || !strcmp(c,"LOANS") ||
           !strcmp(c,"FEEDBACK_LIST") || !strcmp(c,"FEEDBACK_REVIEW") ||
           !strcmp(c,"PASSWORD");
}
