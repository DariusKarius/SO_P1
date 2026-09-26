#include "mpt_internal.h"

static void boost_owner_priority(mpt_thread_t *owner, int new_prio) {
    owner->cur_prio = new_prio;
}

void mpt_mutex_init(mpt_mutex_t *m, mpt_protocol_t protocol, int ceiling) {
    m->owner = NULL;
    m->protocol = protocol;
    m->ceiling = ceiling;
    m->saved_prio = 0;
    m->wait_head = NULL;
}

void mpt_mutex_lock(mpt_mutex_t *m) {
    mpt__enter_kernel();
    mpt_thread_t *self = g_current;

    if (m->owner == NULL) {
        m->owner = self;
        if (m->protocol == MPT_CEILING && m->ceiling > self->cur_prio) {
            m->saved_prio = self->cur_prio;
            self->cur_prio = m->ceiling;
        }
        if (mpt__sched_is_mutex_switch()) {
            self->state = MPT_READY;
            mpt__rq_enqueue(self);
            mpt__force_dispatch();
        }
    } else {
        if (m->protocol == MPT_INHERIT && self->cur_prio > m->owner->cur_prio)
            boost_owner_priority(m->owner, self->cur_prio);

        self->state = MPT_BLOCKED;
        mpt__wlist_add(&m->wait_head, self);
        mpt__force_dispatch();
    }

    mpt__leave_kernel();
}

void mpt_mutex_unlock_locked(mpt_mutex_t *m) {
    mpt_thread_t *self = g_current;

    if (m->protocol == MPT_CEILING)
        self->cur_prio = m->saved_prio;
    else if (m->protocol == MPT_INHERIT)
        self->cur_prio = self->base_prio;

    mpt_thread_t *waiter = mpt__wlist_remove_highest(&m->wait_head);
    m->owner = waiter;
    if (waiter) {
        waiter->state = MPT_READY;
        mpt__rq_enqueue(waiter);
    }
}

void mpt_mutex_unlock(mpt_mutex_t *m) {
    mpt__enter_kernel();
    mpt_mutex_unlock_locked(m);
    mpt__force_dispatch();
    mpt__leave_kernel();
}