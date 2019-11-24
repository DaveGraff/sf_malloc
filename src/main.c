#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    sf_mem_init();

    // double* ptr = sf_malloc(sizeof(double));

    // *ptr = 320320320e-320;

    // sf_free(ptr);

    void *x = sf_malloc(sizeof(int));
    /* void *y = */ sf_malloc(10);
    x = sf_realloc(x, sizeof(int) * 10);
    // sf_show_heap();

    sf_mem_fini();

    return EXIT_SUCCESS;
}
