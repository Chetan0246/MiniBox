#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int target_mb = 32;
    if (argc > 1) {
        target_mb = atoi(argv[1]);
        if (target_mb <= 0) target_mb = 32;
    }

    printf("MEMORY_HOG_STARTING: target=%d MB\n", target_mb);
    fflush(stdout);

    for (int i = 1; i <= target_mb; i++) {
        char *chunk = malloc(1024 * 1024);
        if (!chunk) {
            printf("MALLOC_FAILED: at %d MB\n", i);
            fflush(stdout);
            return 1;
        }
        /* Dirty the page to force the kernel to allocate real physical RAM */
        memset(chunk, 0x42, 1024 * 1024);
        printf("ALLOCATED_MB: %d\n", i);
        fflush(stdout);
        usleep(10000); /* 10ms */
    }

    printf("MEMORY_HOG_DONE: Successfully allocated %d MB\n", target_mb);
    fflush(stdout);

    if (argc > 2) {
        int hold_sec = atoi(argv[2]);
        if (hold_sec > 0) {
            printf("HOLDING_ALLOCATION: %d seconds\n", hold_sec);
            fflush(stdout);
            sleep(hold_sec);
        }
    }

    return 0;
}
