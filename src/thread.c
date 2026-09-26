#define _XOPEN_SOURCE 700
#include "mpt_internal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

mpt_thread_t *g_current = NULL;

static mpt_thread_t *rq_head = NULL, *rq_tail = NULL;
static int rq_count = 0;

static ucontext_t g_return_ctx;

static volatile sig_atomic_t g_kernel_flag = 0;
static volatile sig_atomic_t g_dispatcher_flag = 0;
static volatile sig_atomic_t g_deferred_signal = 0;

static int g_monitor_enabled = 1;
static mpt_sched_policy_t g_sched_policy = MPT_SCHED_FIFO;
static unsigned int g_rand_state = 12345u;

static int g_context_switches = 0;
static int g_stat_signals = 0;
static int g_stat_deferred = 0;
static int g_active_threads = 0;
static int g_next_id = 0;
static int g_threads_created = 0;
static int g_threads_exited = 0;

static sigset_t g_alarm_set;
static int g_preemption_on = 0;

static void widen_window_if_unprotected(void) {
    if (!g_monitor_enabled) {
        for (volatile int i = 0; i < 4000; i++) {
            /* nada */
        }
    }
}

void mpt__rq_enqueue(mpt_thread_t *t) {
    t->next = NULL;
    if (rq_head == NULL) {
        rq_head = rq_tail = t;
        rq_count++;
        return;
    }
    mpt_thread_t *old_tail = rq_tail;
    widen_window_if_unprotected();
    old_tail->next = t;
    rq_tail = t;
    rq_count++;
}

static void rq_remove_node(mpt_thread_t *prev, mpt_thread_t *node) {
    if (prev)
        prev->next = node->next;
    else
        rq_head = node->next;

    if (node == rq_tail)
        rq_tail = prev;

    node->next = NULL;
    rq_count--;
}

static mpt_thread_t *rq_peek_highest(void) {
    if (!rq_head)
        return NULL;

    mpt_thread_t *best = rq_head;
    for (mpt_thread_t *n = rq_head->next; n; n = n->next) {
        if (n->cur_prio > best->cur_prio)
            best = n;
    }
    return best;
}

static mpt_thread_t *pick_next(void) {
    if (!rq_head)
        return NULL;

    if (g_sched_policy == MPT_SCHED_RANDOM) {
        int n = 0;
        for (mpt_thread_t *c = rq_head; c; c = c->next)
            n++;

        int idx = (int)(rand_r(&g_rand_state) % (unsigned)n);
        mpt_thread_t *prev = NULL, *cur = rq_head;
        for (int i = 0; i < idx; i++) {
            prev = cur;
            cur = cur->next;
        }
        rq_remove_node(prev, cur);
        return cur;
    }

    mpt_thread_t *prev = NULL, *cur = rq_head;
    mpt_thread_t *best = rq_head, *best_prev = NULL;
    while (cur) {
        if (cur->cur_prio > best->cur_prio) {
            best = cur;
            best_prev = prev;
        }
        prev = cur;
        cur = cur->next;
    }
    rq_remove_node(best_prev, best);
    return best;
}

void mpt__wlist_add(mpt_thread_t **head, mpt_thread_t *t) {
    t->next = *head;
    *head = t;
}

mpt_thread_t *mpt__wlist_remove_highest(mpt_thread_t **head) {
    if (!*head)
        return NULL;

    mpt_thread_t *prev = NULL, *cur = *head;
    mpt_thread_t *best = cur, *best_prev = NULL;
    while (cur) {
        if (cur->cur_prio > best->cur_prio) {
            best = cur;
            best_prev = prev;
        }
        prev = cur;
        cur = cur->next;
    }

    if (best_prev)
        best_prev->next = best->next;
    else
        *head = best->next;

    best->next = NULL;
    return best;
}

static void do_switch(mpt_thread_t *prev, mpt_thread_t *next) {
    if (next) {
        sigdelset(&next->ctx.uc_sigmask, SIGALRM);
        next->state = MPT_RUNNING;
        next->cs_count++;
    }
    g_current = next;
    g_context_switches++;

    if (prev && next)
        swapcontext(&prev->ctx, &next->ctx);
    else if (prev && !next)
        swapcontext(&prev->ctx, &g_return_ctx);
    else if (!prev && next)
        swapcontext(&g_return_ctx, &next->ctx);
}

static void apply_perverted_force(void) {
    if (!g_current || g_current->state != MPT_RUNNING)
        return;

    if (g_sched_policy == MPT_SCHED_RR_ORDERED) {
        g_current->state = MPT_READY;
        mpt__rq_enqueue(g_current);
        g_dispatcher_flag = 1;
    } else if (g_sched_policy == MPT_SCHED_RANDOM) {
        if (rand_r(&g_rand_state) & 1) {
            g_current->state = MPT_READY;
            mpt__rq_enqueue(g_current);
            g_dispatcher_flag = 1;
        }
    }
}

void mpt__force_dispatch(void) {
    g_dispatcher_flag = 1;
}

int mpt__sched_is_mutex_switch(void) {
    return g_sched_policy == MPT_SCHED_MUTEX_SWITCH;
}

void mpt__enter_kernel(void) {
    g_kernel_flag = 1;
}

void mpt__leave_kernel(void) {
    if (g_current == NULL) {
        g_dispatcher_flag = 0;
        g_kernel_flag = 0;
        return;
    }

    if (!g_monitor_enabled) {
        g_kernel_flag = 0;
        if (g_dispatcher_flag) {
            g_dispatcher_flag = 0;
            mpt_thread_t *next = pick_next();
            if (next != g_current)
                do_switch(g_current, next);
            else if (next)
                next->state = MPT_RUNNING;
        }
        return;
    }

    apply_perverted_force();

    for (;;) {
        if (!g_dispatcher_flag) {
            g_kernel_flag = 0;
            return;
        }
        g_dispatcher_flag = 0;

        mpt_thread_t *self = g_current;
        if (self && self->state == MPT_RUNNING) {
            mpt_thread_t *best_ready = rq_peek_highest();
            if (!best_ready || best_ready->cur_prio <= self->cur_prio) {
                if (!g_deferred_signal) {
                    g_kernel_flag = 0;
                    return;
                }
                g_deferred_signal = 0;
                continue;
            }
            self->state = MPT_READY;
            mpt__rq_enqueue(self);
        }

        mpt_thread_t *next = pick_next();
        if (next != g_current)
            do_switch(g_current, next);
        else if (next)
            next->state = MPT_RUNNING;

        if (!g_deferred_signal) {
            g_kernel_flag = 0;
            return;
        }
        g_deferred_signal = 0;
    }
}

static void universal_signal_handler(int signo) {
    (void)signo;
    g_stat_signals++;

    if (g_monitor_enabled && g_kernel_flag) {
        g_stat_deferred++;
        g_deferred_signal = 1;
        g_dispatcher_flag = 1;
        return;
    }

    mpt__enter_kernel();
    if (g_current && g_current->state == MPT_RUNNING) {
        g_current->state = MPT_READY;
        mpt__rq_enqueue(g_current);
    }
    g_dispatcher_flag = 1;
    mpt__leave_kernel();
}

void mpt_enable_preemption(int usec_period) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = universal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    sigemptyset(&g_alarm_set);
    sigaddset(&g_alarm_set, SIGALRM);

    struct itimerval it;
    it.it_value.tv_sec = usec_period / 1000000;
    it.it_value.tv_usec = usec_period % 1000000;
    it.it_interval = it.it_value;
    setitimer(ITIMER_REAL, &it, NULL);
    g_preemption_on = 1;
}

void mpt_disable_preemption(void) {
    if (!g_preemption_on)
        return;

    struct itimerval it;
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_REAL, &it, NULL);
    signal(SIGALRM, SIG_IGN);
    g_preemption_on = 0;
}

static void trampoline(void) {
    mpt_thread_t *self = g_current;
    self->entry(self->arg);
    mpt_exit();
}

mpt_thread_t *mpt_create(void (*entry)(void *), void *arg, int priority) {
    mpt_thread_t *t = calloc(1, sizeof(*t));
    t->id = g_next_id++;
    t->base_prio = t->cur_prio = priority;
    t->entry = entry;
    t->arg = arg;
    t->stack = malloc(MPT_STACK_SIZE);
    t->state = MPT_READY;

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = MPT_STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, (void (*)(void))trampoline, 0);

    mpt__enter_kernel();
    mpt__rq_enqueue(t);
    g_active_threads++;
    g_threads_created++;
    g_dispatcher_flag = 1;
    mpt__leave_kernel();

    return t;
}

void mpt_yield(void) {
    mpt__enter_kernel();
    mpt_thread_t *self = g_current;
    self->state = MPT_READY;
    mpt__rq_enqueue(self);
    g_dispatcher_flag = 1;
    mpt__leave_kernel();
}

void mpt_join(mpt_thread_t *t) {
    mpt__enter_kernel();
    if (!t->has_exited) {
        t->join_waiter = g_current;
        g_current->state = MPT_BLOCKED;
        g_dispatcher_flag = 1;
    }
    mpt__leave_kernel();
}

void mpt_exit(void) {
    mpt__enter_kernel();
    mpt_thread_t *self = g_current;
    self->state = MPT_TERMINATED;
    self->has_exited = 1;
    g_active_threads--;
    g_threads_exited++;

    if (self->join_waiter) {
        self->join_waiter->state = MPT_READY;
        mpt__rq_enqueue(self->join_waiter);
        self->join_waiter = NULL;
    }

    g_dispatcher_flag = 1;
    mpt__leave_kernel();

    for (;;) {
        /* bucle infinito tras terminar */
    }
}

void mpt_run(void) {
    mpt_thread_t *next = pick_next();
    if (next)
        do_switch(NULL, next);
}

void mpt_set_monitor_enabled(int enabled) {
    g_monitor_enabled = enabled;
}

void mpt_set_sched_policy(mpt_sched_policy_t p) {
    g_sched_policy = p;
}

void mpt_set_random_seed(unsigned int seed) {
    g_rand_state = seed;
}

int mpt_context_switches(void) {
    return g_context_switches;
}

int mpt_current_id(void) {
    return g_current ? g_current->id : -1;
}

int mpt_stat_signals(void) {
    return g_stat_signals;
}

int mpt_stat_deferred(void) {
    return g_stat_deferred;
}

int mpt_threads_created(void) {
    return g_threads_created;
}

int mpt_threads_exited(void) {
    return g_threads_exited;
}

int mpt_check_queue_integrity(void) {
    int walked = 0;
    int bound = rq_count + 32;
    mpt_thread_t *n = rq_head;

    while (n && walked <= bound) {
        walked++;
        n = n->next;
    }

    if (n != NULL)
        return 0;

    return walked == rq_count;
}

void mpt_reset(void) {
    rq_head = rq_tail = NULL;
    rq_count = 0;
    g_current = NULL;
    g_kernel_flag = 0;
    g_dispatcher_flag = 0;
    g_deferred_signal = 0;
    g_context_switches = 0;
    g_stat_signals = 0;
    g_stat_deferred = 0;
    g_active_threads = 0;
    g_threads_created = 0;
    g_threads_exited = 0;
    g_sched_policy = MPT_SCHED_FIFO;
    g_monitor_enabled = 1;
}