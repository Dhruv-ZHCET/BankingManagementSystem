#include <string.h>
#include "role_modules.h"
int customer_command_allowed(const char *c) {
    if (!c) return 0;
    return !strcmp(c,"BALANCE") || !strcmp(c,"DEPOSIT") ||
           !strcmp(c,"WITHDRAW") || !strcmp(c,"TRANSFER") ||
           !strcmp(c,"LOAN_APPLY") || !strcmp(c,"LOANS") ||
           !strcmp(c,"PASSWORD") || !strcmp(c,"FEEDBACK") ||
           !strcmp(c,"HISTORY");
}
