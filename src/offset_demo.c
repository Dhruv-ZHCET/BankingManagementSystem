#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd = open("data/demo.txt",
                  O_WRONLY | O_CREAT | O_TRUNC,
                  0600);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    if (write(fd, "ABCDEF", 6) != 6) {
        perror("write");
        close(fd);
        return 1;
    }

    off_t position = lseek(fd, 2, SEEK_SET);

    if (position == (off_t)-1) {
        perror("lseek");
        close(fd);
        return 1;
    }

    if (write(fd, "X", 1) != 1) {
        perror("write");
        close(fd);
        return 1;
    }

    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;

}