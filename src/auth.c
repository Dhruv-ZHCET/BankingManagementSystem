#include "auth.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SALT_BYTES 16
#define HASH_BYTES 32
#define PBKDF2_ITERATIONS 200000
#define USERS_FILE_LINE 256

typedef struct
{
    char username[AUTH_USERNAME_MAX + 1];
    unsigned char salt[SALT_BYTES];
    unsigned char hash[HASH_BYTES];
    UserRole role;
    int user_id;
    int account_number;
    int active;
} UserRecord;

static pthread_mutex_t auth_mutex = PTHREAD_MUTEX_INITIALIZER;
static char active_sessions[AUTH_MAX_SESSIONS][AUTH_USERNAME_MAX + 1];

static void to_hex(const unsigned char *input, size_t length, char *output)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++)
    {
        output[i * 2] = digits[input[i] >> 4];
        output[i * 2 + 1] = digits[input[i] & 0x0f];
    }
    output[length * 2] = '\0';
}

static int from_hex(const char *input, unsigned char *output, size_t length)
{
    if (strlen(input) != length * 2)
        return -1;

    for (size_t i = 0; i < length; i++)
    {
        unsigned int byte;
        if (sscanf(input + i * 2, "%2x", &byte) != 1)
            return -1;
        output[i] = (unsigned char)byte;
    }
    return 0;
}

static int password_hash(const char *password,
                         const unsigned char salt[SALT_BYTES],
                         unsigned char output[HASH_BYTES])
{
    if (password == NULL || strlen(password) < 1 || strlen(password) > 128)
        return -1;

    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                             salt, SALT_BYTES, PBKDF2_ITERATIONS,
                             EVP_sha256(), HASH_BYTES, output) == 1
               ? 0
               : -1;
}

static int write_user_record(FILE *file, const char *username,
                             const char *password, int user_id,
                             UserRole role, int account_number)
{
    UserRecord record = {0};
    char salt_hex[SALT_BYTES * 2 + 1];
    char hash_hex[HASH_BYTES * 2 + 1];

    if (strlen(username) > AUTH_USERNAME_MAX ||
        RAND_bytes(record.salt, SALT_BYTES) != 1 ||
        password_hash(password, record.salt, record.hash) != 0)
        return -1;

    to_hex(record.salt, SALT_BYTES, salt_hex);
    to_hex(record.hash, HASH_BYTES, hash_hex);

    return fprintf(file, "%s|%s|%s|%d|%d|%d|1\n", username,
                   salt_hex, hash_hex, user_id, (int)role, account_number) < 0
               ? -1
               : 0;
}

int auth_init(const char *users_path)
{
    if (users_path == NULL)
        return -1;

    if (access(users_path, F_OK) == 0)
        return 0;

    FILE *file = fopen(users_path, "wx");
    if (file)
        (void)fchmod(fileno(file), 0600);
    if (file == NULL)
    {
        /* Another server startup may have created it between access/fopen. */
        return errno == EEXIST ? 0 : -1;
    }

    int result = 0;
    if (write_user_record(file, "customer1", "cust123", 1001, ROLE_CUSTOMER, 1001) != 0 ||
        write_user_record(file, "employee1", "emp123", 2001, ROLE_BANK_EMPLOYEE, 0) != 0 ||
        write_user_record(file, "manager1", "manager123", 3001, ROLE_MANAGER, 0) != 0 ||
        write_user_record(file, "admin", "admin123", 4001, ROLE_ADMINISTRATOR, 0) != 0 ||
        fflush(file) != 0 || fsync(fileno(file)) != 0)
        result = -1;

    if (fclose(file) != 0)
        result = -1;

    if (result != 0)
        unlink(users_path);

    return result;
}

static int parse_record(char *line, UserRecord *record)
{
    char salt_hex[SALT_BYTES * 2 + 1];
    char hash_hex[HASH_BYTES * 2 + 1];
    int role;
    int fields = sscanf(line, "%31[^|]|%32[^|]|%64[^|]|%d|%d|%d|%d",
                        record->username, salt_hex, hash_hex,
                        &record->user_id, &role, &record->account_number,
                        &record->active);

    if (fields != 7 || record->user_id <= 0 ||
        role < ROLE_CUSTOMER || role > ROLE_ADMINISTRATOR ||
        record->account_number < 0 ||
        (record->active != 0 && record->active != 1) ||
        from_hex(salt_hex, record->salt, SALT_BYTES) != 0 ||
        from_hex(hash_hex, record->hash, HASH_BYTES) != 0)
        return -1;

    record->role = (UserRole)role;
    return 0;
}

static int session_exists(const char *username)
{
    for (size_t i = 0; i < AUTH_MAX_SESSIONS; i++)
    {
        if (strcmp(active_sessions[i], username) == 0)
            return 1;
    }
    return 0;
}

static int reserve_session(const char *username)
{
    if (session_exists(username))
        return -1;

    for (size_t i = 0; i < AUTH_MAX_SESSIONS; i++)
    {
        if (active_sessions[i][0] == '\0')
        {
            strcpy(active_sessions[i], username);
            return 0;
        }
    }
    return -1;
}

int auth_login(const char *users_path, const char *username,
               const char *password, AuthenticatedUser *authenticated)
{
    if (users_path == NULL || username == NULL || password == NULL ||
        authenticated == NULL || username[0] == '\0' ||
        strlen(username) > AUTH_USERNAME_MAX || strlen(password) > 128)
        return -1;

    int result = -1;
    pthread_mutex_lock(&auth_mutex);

    FILE *file = fopen(users_path, "r");
    if (file == NULL)
        goto done;

    char line[USERS_FILE_LINE];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        UserRecord record = {0};
        if (parse_record(line, &record) != 0)
            continue;
        if (strcmp(record.username, username) != 0 || !record.active)
            continue;

        unsigned char candidate[HASH_BYTES];
        if (password_hash(password, record.salt, candidate) != 0)
            break;

        if (CRYPTO_memcmp(candidate, record.hash, HASH_BYTES) != 0)
            break;

        if (reserve_session(record.username) != 0)
            break;

        strcpy(authenticated->username, record.username);
        authenticated->user.user_id = record.user_id;
        authenticated->user.role = record.role;
        authenticated->user.account_number = record.account_number;
        result = 0;
        break;
    }

    fclose(file);

done:
    pthread_mutex_unlock(&auth_mutex);
    return result;
}

void auth_logout(const char *username)
{
    if (username == NULL)
        return;

    pthread_mutex_lock(&auth_mutex);
    for (size_t i = 0; i < AUTH_MAX_SESSIONS; i++)
    {
        if (strcmp(active_sessions[i], username) == 0)
        {
            active_sessions[i][0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&auth_mutex);
}

static int write_record_existing(FILE *f, const UserRecord *r)
{
    char salt[SALT_BYTES * 2 + 1], hash[HASH_BYTES * 2 + 1];
    to_hex(r->salt, SALT_BYTES, salt);
    to_hex(r->hash, HASH_BYTES, hash);
    return fprintf(f, "%s|%s|%s|%d|%d|%d|%d\n", r->username, salt, hash,
                   r->user_id, (int)r->role, r->account_number, r->active) < 0
               ? -1
               : 0;
}

static int rewrite_user(const char *path, const char *username, const char *new_password,
                        int set_role, UserRole role, int set_active, int active)
{
    if (!path || !username)
        return -1;
    pthread_mutex_lock(&auth_mutex);
    FILE *in = fopen(path, "r");
    if (!in)
    {
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    char temp[512];
    int z = snprintf(temp, sizeof temp, "%s.tmp", path);
    if (z < 0 || (size_t)z >= sizeof temp)
    {
        fclose(in);
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    FILE *out = fopen(temp, "w");
    if (out)
        (void)fchmod(fileno(out), 0600);
    if (!out)
    {
        fclose(in);
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    int found = 0, rc = 0;
    char line[USERS_FILE_LINE];
    while (fgets(line, sizeof line, in))
    {
        UserRecord r = {0};
        if (parse_record(line, &r) != 0)
        {
            rc = -1;
            break;
        }
        if (strcmp(r.username, username) == 0)
        {
            found = 1;
            if (new_password)
            {
                if (RAND_bytes(r.salt, SALT_BYTES) != 1 || password_hash(new_password, r.salt, r.hash) != 0)
                {
                    rc = -1;
                    break;
                }
            }
            if (set_role)
                r.role = role;
            if (set_active)
                r.active = active;
        }
        if (write_record_existing(out, &r) != 0)
        {
            rc = -1;
            break;
        }
    }
    if (ferror(in))
        rc = -1;
    if (fflush(out) != 0 || fsync(fileno(out)) != 0)
        rc = -1;
    if (fclose(in) != 0)
        rc = -1;
    if (fclose(out) != 0)
        rc = -1;
    if (!found)
        rc = -1;
    if (rc == 0 && rename(temp, path) != 0)
        rc = -1;
    if (rc != 0)
        unlink(temp);
    pthread_mutex_unlock(&auth_mutex);
    return rc;
}

int auth_add_user(const char *path, const char *username, const char *password,
                  UserRole role, int account_number, int *new_user_id)
{
    if (!path || !username || !password || !new_user_id || !*username ||
        strlen(username) > AUTH_USERNAME_MAX || strchr(username, '|') || strchr(username, ' ') ||
        role < ROLE_CUSTOMER || role > ROLE_ADMINISTRATOR || account_number < 0 ||
        strlen(password) < 1 || strlen(password) > 128)
        return -1;

    pthread_mutex_lock(&auth_mutex);

    FILE *in = fopen(path, "r");
    if (!in)
    {
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    int maxid = 0, duplicate = 0;
    char line[USERS_FILE_LINE];

    while (fgets(line, sizeof line, in))
    {
        UserRecord r = {0};
        if (parse_record(line, &r))
            continue;
        if (r.user_id > maxid)
            maxid = r.user_id;
        if (!strcmp(r.username, username))
            duplicate = 1;
    }
    fclose(in);
    if (duplicate || maxid == INT_MAX)
    {
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    FILE *out = fopen(path, "a");
    if (out)
        (void)fchmod(fileno(out), 0600);
    if (!out)
    {
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }
    int id = maxid + 1;
    int rc = write_user_record(out, username, password, id, role, account_number);
    if (!rc && (fflush(out) != 0 || fsync(fileno(out)) != 0))
        rc = -1;
    if (fclose(out) != 0)
        rc = -1;
    pthread_mutex_unlock(&auth_mutex);
    if (!rc)
        *new_user_id = id;
    return rc;
}
int auth_change_password(const char *p, const char *u, const char *pw)
{
    if (!pw || strlen(pw) < 1 || strlen(pw) > 128)
        return -1;
    return rewrite_user(p, u, pw, 0, ROLE_CUSTOMER, 0, 0);
}
int auth_set_role(const char *p, const char *u, UserRole r)
{
    if (r < ROLE_CUSTOMER || r > ROLE_ADMINISTRATOR)
        return -1;
    return rewrite_user(p, u, NULL, 1, r, 0, 0);
}
int auth_set_active(const char *p, const char *u, int a)
{
    if (a != 0 && a != 1)
        return -1;
    return rewrite_user(p, u, NULL, 0, ROLE_CUSTOMER, 1, a);
}
int auth_list_users(const char *path, char *out, size_t cap)
{
    if (!path || !out || cap < 2)
        return -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    out[0] = '\0';
    size_t used = 0;
    char line[USERS_FILE_LINE];
    int rc = 0;
    while (fgets(line, sizeof line, f))
    {
        UserRecord r = {0};
        if (parse_record(line, &r))
            continue;
        const char *rn = r.role == ROLE_CUSTOMER ? "CUSTOMER" : r.role == ROLE_BANK_EMPLOYEE ? "EMPLOYEE"
                                                            : r.role == ROLE_MANAGER         ? "MANAGER"
                                                                                             : "ADMIN";
        int k = snprintf(out + used, cap - used, "%s%d|%s|%s|%d|%d",
                         used ? ";" : "", r.user_id, r.username, rn,
                         r.account_number, r.active);
        if (k < 0 || (size_t)k >= cap - used)
        {
            rc = -1;
            break;
        }
        used += (size_t)k;
    }
    fclose(f);
    return rc;
}

int auth_get_user_by_id(const char *path, int id, BankingUser *user)
{
    if (!path || id <= 0 || !user)
        return -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[USERS_FILE_LINE];
    int rc = -1;
    while (fgets(line, sizeof line, f))
    {
        UserRecord r = {0};
        if (parse_record(line, &r) != 0 || !r.active || r.user_id != id)
            continue;
        user->user_id = r.user_id;
        user->role = r.role;
        user->account_number = r.account_number;
        rc = 0;
        break;
    }
    fclose(f);
    return rc;
}
