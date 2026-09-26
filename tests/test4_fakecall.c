#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define N_THREADS 2
#define STACK_SIZE (64 * 1024)
#define WRAP_STACK_SIZE (32 * 1024)
#define STEPS_PER_THREAD 5

typedef void (*sig_handler_t)(int who, int sig);

typedef struct {
    int id;
    int priority;
    ucontext_t ctx;
    ucontext_t wrap_ctx;
    char stack[STACK_SIZE];
    char wrap_stack[WRAP_STACK_SIZE];
    int pending_signal;
    sig_handler_t handler;
    int done;
} thread_t;

static thread_t threads[N_THREADS];
static ucontext_t main_ctx;
static int current = -1;
static int next_target = 0;
static volatile sig_atomic_t alarm_hits = 0;

static void universal_signal_handler(int signo) {
    alarm_hits++;
    int target = next_target;
    next_target = (next_target + 1) % N_THREADS;

    if (current == target)
        printf("  [SIGALRM #%d] llega mientras corre el propio hilo %d -> "
               "se difiere igual, se atendera en su proximo despacho\n",
               (int)alarm_hits, target);
    else
        printf("  [SIGALRM #%d] llega mientras corre hilo %d -> dirigida al "
               "hilo %d (que NO esta corriendo): se DIFIERE\n",
               (int)alarm_hits, current, target);

    threads[target].pending_signal = signo;
}

static thread_t *wrapping = NULL;

static void fake_call_wrapper(void) {
    thread_t *t = wrapping;
    printf("    -> [fake call] hilo %d ejecuta AHORA su handler para la "
           "signal %d (prioridad %d), justo antes de reanudar su codigo\n",
           t->id, t->pending_signal, t->priority);
    t->handler(t->id, t->pending_signal);
    t->pending_signal = 0;
    printf("    <- [fake call] hilo %d retorna exactamente al punto donde "
           "habia quedado\n",
           t->id);
    setcontext(&t->ctx);
}

static void dispatch(int target) {
    current = target;
    thread_t *t = &threads[target];

    if (t->pending_signal) {
        getcontext(&t->wrap_ctx);
        t->wrap_ctx.uc_stack.ss_sp = t->wrap_stack;
        t->wrap_ctx.uc_stack.ss_size = WRAP_STACK_SIZE;
        t->wrap_ctx.uc_link = NULL;
        wrapping = t;
        makecontext(&t->wrap_ctx, fake_call_wrapper, 0);
        swapcontext(&main_ctx, &t->wrap_ctx);
    } else {
        swapcontext(&main_ctx, &t->ctx);
    }
}

static void mpt_yield(void) {
    swapcontext(&threads[current].ctx, &main_ctx);
}

static void worker(void) {
    thread_t *self = &threads[current];
    for (int i = 1; i <= STEPS_PER_THREAD; i++) {
        printf("hilo %d: paso %d/%d (prioridad %d)\n",
               self->id, i, STEPS_PER_THREAD, self->priority);
        mpt_yield();
    }
    self->done = 1;
    printf("hilo %d: termino\n", self->id);
    mpt_yield();
}

static void thread_entry(void) {
    worker();
}

static void mi_handler(int who, int sig) {
    printf("       (handler de usuario) hilo %d atiende la señal %d: "
           "actualiza su estado interno y sigue\n",
           who, sig);
}

int main(void) {
    printf("Demo 4: fake calls (Figura 3 del paper de Mueller)\n");
    printf("%d hilos cooperativos; SIGALRM real cada 150 ms, dirigida por "
           "turnos a un hilo distinto del que este corriendo\n\n",
           N_THREADS);

    for (int i = 0; i < N_THREADS; i++) {
        threads[i].id = i;
        threads[i].priority = i + 1;
        threads[i].pending_signal = 0;
        threads[i].handler = mi_handler;
        threads[i].done = 0;
        getcontext(&threads[i].ctx);
        threads[i].ctx.uc_stack.ss_sp = threads[i].stack;
        threads[i].ctx.uc_stack.ss_size = STACK_SIZE;
        threads[i].ctx.uc_link = &main_ctx;
        makecontext(&threads[i].ctx, thread_entry, 0);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = universal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 150000;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 150000;
    setitimer(ITIMER_REAL, &it, NULL);

    int all_done = 0;
    int rr = 0;
    while (!all_done) {
        dispatch(rr);
        rr = (rr + 1) % N_THREADS;
        all_done = 1;
        for (int i = 0; i < N_THREADS; i++) {
            if (!threads[i].done)
                all_done = 0;
        }
        struct timespec ts = {0, 40000000L};
        nanosleep(&ts, NULL);
    }

    it.it_value.tv_usec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);

    printf("\ntest4_fakecall: OK\n");
    return 0;
}