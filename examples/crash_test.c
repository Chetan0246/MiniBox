#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    int sig = SIGSEGV;
    if (argc > 1) {
        sig = atoi(argv[1]);
    }
    printf("Crash test: raising signal %d\n", sig);
    fflush(stdout);
    raise(sig);
    return 0;
}
