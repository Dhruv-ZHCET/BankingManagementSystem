#ifndef ROLE_MODULES_H
#define ROLE_MODULES_H

int customer_command_allowed(const char *command);

int employee_command_allowed(const char *command);

int manager_command_allowed(const char *command);

int administrator_command_allowed(const char *command);

#endif
