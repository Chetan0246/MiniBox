#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int duration_sec = 1;
    if (argc > 1) {
        duration_sec = atoi(argv[1]);
        if (duration_sec <= 0) duration_sec = 1;
    }

    struct timespec start_real, end_real;
    struct timespec start_cpu, end_cpu;

    clock_gettime(CLOCK_MONOTONIC, &start_real);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_cpu);

    /* Burn CPU for duration_sec real seconds */
    volatile unsigned long counter = 0;
    while (1) {
        counter++;
        if ((counter % 1000000) == 0) {
            clock_gettime(CLOCK_MONOTONIC, &end_real);
            double elapsed = (end_real.tv_sec - start_real.tv_sec) +
                             (end_real.tv_nsec - start_real.tv_nsec) / 1e9;
            if (elapsed >= (double)duration_sec) {
                break;
            }
        }
    }

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_cpu);
    double cpu_used = (end_cpu.tv_sec - start_cpu.tv_sec) +
                      (end_cpu.tv_nsec - start_cpu.tv_nsec) / 1e9;
    double real_used = (end_real.tv_sec - start_real.tv_sec) +
                       (end_real.tv_nsec - start_real.tv_nsec) / 1e9;

    printf("CPU_HOG_DONE: real=%.3fs cpu=%.3fs usage=%.1f%%\n",
           real_used, cpu_used, (real_used > 0.0) ? (cpu_used / real_used) * 100.0 : 0.0);
    return 0;
}
