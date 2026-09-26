#include "../src/mpt.h"
#include <assert.h>
#include <stdio.h>

static mpt_mutex_t mutex;
static mpt_cond_t cond;
static int item_ready = 0;

static void producer(void *arg) {
    (void)arg;
    mpt_mutex_lock(&mutex);
    item_ready = 1;
    mpt_cond_signal(&cond);
    mpt_mutex_unlock(&mutex);
}

static void consumer(void *arg) {
    (void)arg;
    mpt_mutex_lock(&mutex);
    while (!item_ready) {
        mpt_cond_wait(&cond, &mutex);
    }
    assert(item_ready == 1);
    mpt_mutex_unlock(&mutex);
}

int main(void) {
    mpt_reset();
    mpt_mutex_init(&mutex, MPT_INHERIT, 0);
    mpt_cond_init(&cond);

    mpt_create(consumer, NULL, 1);
    mpt_create(producer, NULL, 1);

    mpt_run();

    printf("test1_monitor: OK\n");
    return 0;
}