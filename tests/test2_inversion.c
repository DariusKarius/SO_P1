#include "../src/mpt.h"
#include <assert.h>
#include <stdio.h>

static mpt_mutex_t mutex;
static int p3_ran = 0;

static void p2_medium(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++)
        mpt_yield();
}

static void p3_high(void *arg) {
    (void)arg;
    mpt_mutex_lock(&mutex);
    p3_ran = 1;
    mpt_mutex_unlock(&mutex);
}

static void p1_low(void *arg) {
    (void)arg;
    mpt_mutex_lock(&mutex);
    mpt_create(p3_high, NULL, 3);
    mpt_create(p2_medium, NULL, 2);
    
    mpt_yield();
    mpt_mutex_unlock(&mutex);
}

int main(void) {
    mpt_reset();
    mpt_mutex_init(&mutex, MPT_INHERIT, 0);
    
    mpt_create(p1_low, NULL, 1);
    mpt_run();

    assert(p3_ran == 1);
    printf("test2_inversion: OK\n");
    return 0;
}