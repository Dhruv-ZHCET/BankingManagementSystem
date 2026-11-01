#include <string.h>
#include "role_modules.h"
int administrator_command_allowed(const char *c) {
    if (!c) return 0;
    return !strcmp(c,"USER_ADD") || !strcmp(c,"USER_ROLE") ||
           !strcmp(c,"USER_ACTIVE") || !strcmp(c,"USER_LIST") ||
           !strcmp(c,"PASSWORD") || !strcmp(c,"ACCOUNTS") ||
           !strcmp(c,"MODIFY_ACCOUNT") || !strcmp(c,"CLOSE_ACCOUNT");
}
