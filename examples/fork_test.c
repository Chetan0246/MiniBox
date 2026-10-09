#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int max_attempts = 20;
    if (argc > 1) {
        max_attempts = atoi(argv[1]);
        if (max_attempts <= 0) max_attempts = 20;
    }

    pid_t children[100];
    int count = 0;
    int hit_eagain = 0;

    for (int i = 0; i < max_attempts && count < 100; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            if (errno == EAGAIN) {
                printf("FORK_LIMIT_REACHED: fork #%d failed with EAGAIN (%s)\n", i + 1, strerror(errno));
                hit_eagain = 1;
            } else {
                printf("FORK_FAILED: fork #%d failed with errno %d (%s)\n", i + 1, errno, strerror(errno));
            }
            break;
        } else if (pid == 0) {
            /* Child process: sleep briefly so parent can count active processes */
            usleep(500000); /* 500ms */
            _exit(0);
        } else {
            children[count++] = pid;
        }
    }

    printf("ACTIVE_FORKS: %d\n", count);

    /* Clean up all children */
    for (int i = 0; i < count; i++) {
        int status;
        while (waitpid(children[i], &status, 0) < 0 && errno == EINTR);
    }

    /* Return exit code 0 if we hit the limit as expected, or 2 if unconstrained */
    return hit_eagain ? 0 : 2;
}
