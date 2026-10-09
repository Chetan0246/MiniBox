#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void handle_sigterm(int sig) {
    (void)sig;
    printf("[sleeper] Received SIGTERM! Gracefully shutting down...\n");
    fflush(stdout);
    g_running = 0;
}

int main(int argc, char *argv[]) {
    int duration = 5;
    if (argc > 1) {
        duration = atoi(argv[1]);
    }

    signal(SIGTERM, handle_sigterm);

    printf("[sleeper] Started PID %d. Will sleep for %d seconds.\n", getpid(), duration);
    fflush(stdout);

    for (int i = 0; i < duration && g_running; i++) {
        sleep(1);
    }

    if (!g_running) {
        printf("[sleeper] Terminated gracefully on signal.\n");
        fflush(stdout);
        return 0;
    }

    printf("[sleeper] Completed all %d seconds normally.\n", duration);
    fflush(stdout);
    return 0;
}
