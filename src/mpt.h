#ifndef MPT_H
#define MPT_H

#include <ucontext.h>
#include <stddef.h>

#define MPT_STACK_SIZE (64 * 1024)

typedef enum { MPT_NONE = 0, MPT_INHERIT = 1, MPT_CEILING = 2 } mpt_protocol_t;

typedef enum {
    MPT_SCHED_FIFO = 0,
    MPT_SCHED_MUTEX_SWITCH,
    MPT_SCHED_RR_ORDERED,
    MPT_SCHED_RANDOM
} mpt_sched_policy_t;

typedef enum { MPT_READY, MPT_RUNNING, MPT_BLOCKED, MPT_TERMINATED } mpt_state_t;

typedef struct mpt_thread {
    int id;
    int base_prio;
    int cur_prio;
    mpt_state_t state;
    ucontext_t ctx;
    char *stack;
    void (*entry)(void *arg);
    void *arg;
    struct mpt_thread *next;
    struct mpt_thread *join_waiter;
    int has_exited;
    int cs_count;
} mpt_thread_t;

typedef struct mpt_mutex {
    mpt_thread_t *owner;
    mpt_protocol_t protocol;
    int ceiling;
    int saved_prio;
    mpt_thread_t *wait_head;
} mpt_mutex_t;

typedef struct mpt_cond {
    mpt_thread_t *wait_head;
} mpt_cond_t;

mpt_thread_t *mpt_create(void (*entry)(void *), void *arg, int priority);
void mpt_yield(void);
void mpt_join(mpt_thread_t *t);
void mpt_exit(void);
void mpt_run(void);
void mpt_mutex_init(mpt_mutex_t *m, mpt_protocol_t protocol, int ceiling);
void mpt_mutex_lock(mpt_mutex_t *m);
void mpt_mutex_unlock(mpt_mutex_t *m);
void mpt_cond_init(mpt_cond_t *c);
void mpt_cond_wait(mpt_cond_t *c, mpt_mutex_t *m);
void mpt_cond_signal(mpt_cond_t *c);
void mpt_cond_broadcast(mpt_cond_t *c);

void mpt_set_monitor_enabled(int enabled);
void mpt_set_sched_policy(mpt_sched_policy_t p);
void mpt_set_random_seed(unsigned int seed);
void mpt_enable_preemption(int usec_period);
void mpt_disable_preemption(void);
void mpt_reset(void);

int mpt_context_switches(void);
int mpt_current_id(void);
int mpt_stat_signals(void);
int mpt_stat_deferred(void);
int mpt_check_queue_integrity(void);
int mpt_threads_created(void);
int mpt_threads_exited(void);

#endif
