#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main()
{
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */
    struct timespec start, end;

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        long long kernel_t;
        lseek(fd, 0, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &start);
        kernel_t = write(fd, write_buf, 2); /* recursion w/ cache */
        clock_gettime(CLOCK_MONOTONIC, &end);
        long long kernel_t_u =
            (long long) (end.tv_sec * 1e9 + end.tv_nsec) - kernel_t;
        long long user_t_k =
            kernel_t - (long long) (start.tv_sec * 1e9 + start.tv_nsec);
        printf("%d %lld %lld\n", i, user_t_k, kernel_t_u);
    }

    close(fd);
    return 0;
}
