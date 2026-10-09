#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    printf("Hello from MiniBox example workload!\n");
    printf("PID: %d, PPID: %d\n", getpid(), getppid());
    if (argc > 1) {
        printf("Arguments received: %d\n", argc - 1);
        for (int i = 1; i < argc; i++) {
            printf("  arg[%d]: %s\n", i, argv[i]);
        }
    }
    return 0;
}
