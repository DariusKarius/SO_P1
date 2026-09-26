#include "../src/mpt.h"
#include <assert.h>
#include <stdio.h>

static int executed_threads = 0;

static void worker(void *arg) {
    (void)arg;
    executed_threads++;
}

int main(void) {
    mpt_reset();
    
    /* Configurar o probar la politica encolado aleatorio */
    for (int i = 0; i < 5; i++) {
        mpt_create(worker, NULL, 1);
    }

    mpt_run();

    assert(executed_threads == 5);
    printf("test3_perverted: OK\n");
    return 0;
}