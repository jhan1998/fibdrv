#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main()
{
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        long long sz, tt;
        lseek(fd, i, SEEK_SET);
        sz = write(fd, write_buf, 0); /* recursion w/ cache */
        tt = write(fd, write_buf, 1); /* fast doubling */
        printf("%d %lld %lld\n", i, sz, tt);
    }

    close(fd);
    return 0;
}
