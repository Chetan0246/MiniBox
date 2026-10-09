#include <stdio.h>
#include <unistd.h>
#include <string.h>

static void print_ns(const char *name, const char *path) {
    char link[256] = {0};
    ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n > 0) {
        link[n] = 0;
        printf("  %s namespace: %s\n", name, link);
    } else {
        printf("  %s namespace: unavailable\n", name);
    }
}

int main(void) {
    char host[128] = {0};
    if (gethostname(host, sizeof(host)) != 0) {
        strncpy(host, "unknown", sizeof(host) - 1);
    }

    printf("--- Workload Isolation Inspection ---\n");
    printf("  Hostname: %s\n", host);
    printf("  PID:      %d\n", getpid());
    printf("  PPID:     %d\n", getppid());
    printf("  UID:      %d\n", getuid());
    printf("  GID:      %d\n", getgid());
    print_ns("UTS", "/proc/self/ns/uts");
    print_ns("PID", "/proc/self/ns/pid");
    print_ns("USER", "/proc/self/ns/user");
    print_ns("MNT", "/proc/self/ns/mnt");
    printf("--------------------------------------\n");
    return 0;
}
