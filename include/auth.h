#ifndef AUTH_H
#define AUTH_H

#include "roles.h"
#include <stddef.h>

#define AUTH_USERNAME_MAX 31
#define AUTH_MAX_SESSIONS 100

typedef struct {
    char username[AUTH_USERNAME_MAX + 1];
    BankingUser user;
} AuthenticatedUser;

/* Creates demo users only if data/users.txt does not already exist. */
int auth_init(const char *users_path);

/* Returns 0 on success, -1 on invalid credentials, duplicate session, or error. */
int auth_login(const char *users_path, const char *username,
               const char *password, AuthenticatedUser *authenticated);

/* Releases the active session for this username. */
void auth_logout(const char *username);
int auth_add_user(const char *users_path, const char *username, const char *password, UserRole role, int account_number, int *new_user_id);
int auth_change_password(const char *users_path, const char *username, const char *new_password);
int auth_set_role(const char *users_path, const char *username, UserRole role);
int auth_set_active(const char *users_path, const char *username, int active);
int auth_list_users(const char *users_path, char *out, size_t capacity);
int auth_get_user_by_id(const char *users_path, int user_id, BankingUser *user);

#endif
