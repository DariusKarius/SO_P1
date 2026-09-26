#include "mpt_internal.h"

void mpt_cond_init(mpt_cond_t *c) {
    c->wait_head = NULL;
}

void mpt_cond_wait(mpt_cond_t *c, mpt_mutex_t *m) {
    mpt__enter_kernel();
    mpt_thread_t *self = g_current;
    self->state = MPT_BLOCKED;
    mpt__wlist_add(&c->wait_head, self);

    mpt_mutex_unlock_locked(m);
    mpt__force_dispatch();
    mpt__leave_kernel();
    mpt_mutex_lock(m);
}

void mpt_cond_signal(mpt_cond_t *c) {
    mpt__enter_kernel();
    mpt_thread_t *w = mpt__wlist_remove_highest(&c->wait_head);
    if (w) {
        w->state = MPT_READY;
        mpt__rq_enqueue(w);
        mpt__force_dispatch();
    }
    mpt__leave_kernel();
}

void mpt_cond_broadcast(mpt_cond_t *c) {
    mpt__enter_kernel();
    mpt_thread_t *w;
    int any = 0;

    while ((w = mpt__wlist_remove_highest(&c->wait_head)) != NULL) {
        w->state = MPT_READY;
        mpt__rq_enqueue(w);
        any = 1;
    }

    if (any)
        mpt__force_dispatch();

    mpt__leave_kernel();
}