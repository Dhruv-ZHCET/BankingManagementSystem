#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#define SERVER_PORT 8080
#define LINE_CAPACITY 1024
static int send_all(int fd, const char *m)
{
    size_t n = strlen(m), s = 0;
    while (s < n)
    {
        ssize_t w = send(fd, m + s, n - s, 0);
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!w)
            return -1;
        s += (size_t)w;
    }
    return 0;
}
static int receive_line(int fd, char *b, size_t c)
{
    size_t n = 0;
    while (n + 1 < c)
    {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (!r)
            return n ? -1 : 0;
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ch == '\n')
        {
            b[n] = 0;
            return 1;
        }
        if (ch != '\r')
            b[n++] = ch;
    }
    return -1;
}
static int read_secret(char *out, size_t cap)
{
    struct termios oldt, newt;
    int changed = tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (changed)
    {
        newt = oldt;
        newt.c_lflag &= (tcflag_t)~ECHO;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt) != 0)
            changed = 0;
    }
    int ok = fgets(out, (int)cap, stdin) != NULL;
    if (changed)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
        putchar('\n');
    }
    if (!ok)
        return -1;
    out[strcspn(out, "\r\n")] = 0;
    return 0;
}
int main(void)
{
    int port = SERVER_PORT;
    const char *pe = getenv("BANK_PORT");
    if (pe && *pe)
    {
        char *e = NULL;
        long v = strtol(pe, &e, 10);
        if (!e || *e || v < 1024 || v > 65535)
        {
            fprintf(stderr, "Invalid BANK_PORT\n");
            return 1;
        }
        port = (int)v;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("socket");
        return 1;
    }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0)
    {
        perror("connect");
        close(s);
        return 1;
    }
    char username[32], password[129], line[LINE_CAPACITY], response[LINE_CAPACITY];
    printf("Username: ");
    if (!fgets(username, sizeof username, stdin))
        goto fail;
    username[strcspn(username, "\r\n")] = 0;
    printf("Password (input hidden): ");
    fflush(stdout);
    if (read_secret(password, sizeof password))
        goto fail;
    if (strchr(username, ' ') || strchr(password, ' '))
    {
        puts("Spaces are not supported in credentials.");
        goto fail;
    }
    int n = snprintf(line, sizeof line, "LOGIN %s %s\n", username, password);
    memset(password, 0, sizeof password);
    if (n < 0 || (size_t)n >= sizeof line || send_all(s, line))
        goto fail;
    if (receive_line(s, response, sizeof response) != 1)
        goto fail;
    puts(response);
    if (strncmp(response, "OK LOGIN ", 9))
    {
        close(s);
        return 1;
    }
    puts("Commands (money in paise):\n  BALANCE <account>\n  DEPOSIT <account> <paise>\n  WITHDRAW <account> <paise>\n  TRANSFER <from> <to> <paise>\n  LOAN_APPLY <account> <paise> <purpose text>\n  LOANS\n  HISTORY <account>\n  FEEDBACK <account> <message text>\n  PASSWORD <new-password>\n  CUSTOMER_ADD <username> <password> <name-no-spaces> <initial-paise> (employee)\n  MODIFY_ACCOUNT <account> <name-no-spaces> | CLOSE_ACCOUNT <account> (employee)\n  ACTIVATE_ACCOUNT <account> | DEACTIVATE_ACCOUNT <account> | LOAN_ASSIGN <loan-id> <employee-user-id> | FEEDBACK_LIST (manager)\n  LOAN_PROCESS <loan-id> APPROVE|REJECT (employee)\n  USER_ADD <username> <password> CUSTOMER|EMPLOYEE|MANAGER|ADMIN <account-or-0>\n  USER_ROLE <username> <role> | USER_ACTIVE <username> 0|1 | USER_LIST (admin)\n  ACCOUNTS (admin)\n  LOGOUT | EXIT");
    for (;;)
    {
        printf("bank> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            strcpy(line, "LOGOUT\n");
        else if (!strchr(line, '\n'))
        {
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
            {
            }
            puts("Command too long.");
            continue;
        }
        if (send_all(s, line) || receive_line(s, response, sizeof response) != 1)
            break;
        puts(response);
        if (!strncmp(line, "LOGOUT", 6) || !strncmp(line, "EXIT", 4))
            break;
    }
    close(s);
    return 0;
fail:
    close(s);
    return 1;
}
