#include <stdio.h>
#include <ulib.h>

#define printf(...)                     fprintf(1, __VA_ARGS__)

int
main(void) {
    printf("Hello world!!.\n");
    printf("I am process %d.\n", getpid());
    printf("hello pass.\n");
    return 0;
}

