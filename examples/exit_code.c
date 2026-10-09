#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    int code = 42;
    if (argc > 1) {
        code = atoi(argv[1]);
    }
    printf("Exiting with status code: %d\n", code);
    return code;
}
