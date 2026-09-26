#ifndef MPT_INTERNAL_H
#define MPT_INTERNAL_H

#include "mpt.h"

extern mpt_thread_t *g_current;
void mpt__enter_kernel(void);
void mpt__leave_kernel(void);
void mpt__rq_enqueue(mpt_thread_t *t);
void mpt__wlist_add(mpt_thread_t **head, mpt_thread_t *t);
mpt_thread_t *mpt__wlist_remove_highest(mpt_thread_t **head);
void mpt__force_dispatch(void);
void mpt_mutex_unlock_locked(mpt_mutex_t *m);
int mpt__sched_is_mutex_switch(void);

#endif