#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <ucontext.h>

#define dbg_pr(_x_, ...) do {                                                                  \
                                fprintf(stderr, "[%s][%s:%d]", _x_, __FUNCTION__, __LINE__);   \
                                fprintf(stderr, __VA_ARGS__);                                  \
                            } while (0)

#define dbg_prnt(_x_, ...) do {                                                                 \
                                dbg_pr(_x_, __VA_ARGS__);                                       \
                                fprintf(stderr,"\n");                                           \
                            } while (0)

#define dbg(...) dbg_prnt("DBG", __VA_ARGS__)
#define err(...) dbg_prnt("ERR", __VA_ARGS__)
#define _tprnt(_ath_,_x_,...)                                                                               \
    do {                                                                                                    \
        dbg_pr(_x_, __VA_ARGS__);                                                                           \
        fprintf(stderr, " | thread id = %d | thread_name = %s | thread status = %x[%s]\n",                   \
        _ath_->index, _ath_->name, _ath_->tstatus,                                                          \
        _ath_->tstatus < THREAD_STATUS_INVALID ? thread_status_to_str[_ath_->tstatus] : thread_status_to_str[THREAD_STATUS_INVALID]);   \
    } while (0)

#define tdbg(_ath_,...) _tprnt(_ath_, "DBG", __VA_ARGS__)
#define terr(_ath_,...) _tprnt(_ath_, "ERR", __VA_ARGS__)

#define THREAD_STATUSES     \
TSTATUS(INIT)               \
TSTATUS(RUNNING)            \
TSTATUS(READY)              \
TSTATUS(BLOCKED)            \
TSTATUS(EXIT)               \
TSTATUS(ERROR)              \
TSTATUS(INVALID)            \

#if 0
typedef enum thread_status { THREAD_STATUS_INIT, THREAD_STATUS_RUNNING, THREAD_STATUS_READY,
    THREAD_STATUS_BLOCKED, THREAD_STATUS_EXIT, THREAD_STATUS_ERROR,
    THREAD_STATUS_INVALID} thread_status_t;
#endif

#define TSTATUS(_x_) THREAD_STATUS_##_x_,
typedef enum thread_status { 
    THREAD_STATUSES
}thread_status_t;
#undef TSTATUS

#define TSTATUS(_x_) [THREAD_STATUS_##_x_] = "THREAD_STATUS_"#_x_,
static const char *thread_status_to_str[] = { THREAD_STATUSES };
#undef TSTATUS

#define ASYNC_THREAD_NAME_LEN (255)
#define AYNC_THREAD_STACK_SIZE (16 * 1024)
typedef struct async_thread {
    void (*func)(void *at, void *arg);
    void *arg;
    char name[ASYNC_THREAD_NAME_LEN];
    unsigned int index;
    thread_status_t tstatus;
    ucontext_t this, bcxt;  /*bcxt : context to switch back to scheduler. this : current thread context  from where yield happend */
    unsigned int stack_size;
    char stack[AYNC_THREAD_STACK_SIZE];
} async_thread_t;

#define ASYNC_HANDLE_MAX_TREADS (255)
typedef struct async_handle {
    async_thread_t *threads;
    unsigned int thread_index, max_threads, thread_iter;
} async_handle_t;

/*
**  Initializes async framework.
**  
**/
int async_init(async_handle_t *ah)
{
    if (!ah)
        return -1;
    memset(ah, 0, sizeof(*ah));
    ah->threads = calloc(1,sizeof(async_thread_t) * ASYNC_HANDLE_MAX_TREADS);
    if (!ah->threads) {
        err("memory allocation failed");
        return -1;
    }
    ah->max_threads = ASYNC_HANDLE_MAX_TREADS;
    ah->thread_index = 0;
    return 0;
}

/* yield from current position. save the context and switch back */
int async_yield(async_thread_t *at)
{
    dbg("++ threadid : %d", at->index);
    at->tstatus = THREAD_STATUS_BLOCKED;
    int r = getcontext(&at->this);
    if (r) {
        perror("getcontext");
        return -1;
    }
    if (at->tstatus == THREAD_STATUS_BLOCKED) {
        r = setcontext(&at->bcxt);
        if (r) {
            perror("getcontext");
            return -1;
        }
    } else {
        dbg("returned after setcontext. threadid : %d", at->index);
        at->tstatus = THREAD_STATUS_RUNNING;
    }
    dbg("-- threadid : %d", at->index);
    return 0;
}

/* executing an async thread function after setting up its own stack
*/
int async_func1(async_thread_t *at)
{
    // TDB : set stack pointer to at->stack
    async_yield(at);
    at->func(at, at->arg);
    at->tstatus = THREAD_STATUS_EXIT;
    return 0;
}

void async_func(void *p)
{
    async_thread_t *at = p;
    tdbg(at, "received arguments at = %p", at);
    async_yield(at);
    at->func(at, at->arg);
    at->tstatus = THREAD_STATUS_EXIT;
}

/*
**  Create a new thread with function provided. Function can use async function calls and will be scheduled out if
**  blocking
**/
int async_thread(async_handle_t *ah, async_thread_t **at, void (*tfunc)(void*, void*), void *targ, char *tname)
{
    if (ah->thread_index >= ah->max_threads) {
        err("max threads reached");
        return -1;
    }
    int r;

    async_thread_t *at_tmp = &ah->threads[ah->thread_index];
    r = getcontext(&at_tmp->this);
    if (r) {
        perror("getcontext");
        return -1;
    }
    at_tmp->func = tfunc;
    at_tmp->arg = targ;
    strncpy(at_tmp->name,tname, ASYNC_THREAD_NAME_LEN);
    at_tmp->index = ah->thread_index;
    memset(at_tmp->stack, 0, AYNC_THREAD_STACK_SIZE);
    at_tmp->tstatus = THREAD_STATUS_INIT;
    at_tmp->this.uc_stack.ss_sp = at_tmp->stack;
    at_tmp->stack_size = at_tmp->this.uc_stack.ss_size = AYNC_THREAD_STACK_SIZE;
    tdbg(at_tmp, "making context for thread with func %p, args = %p", async_func, at_tmp);
    makecontext(&at_tmp->this, async_func, 1, at_tmp);
    *at = at_tmp;
    ah->thread_index++;
    return 0;
}


/*
**  Start all the async threads one by one
**/
int async_run(async_handle_t *ah)
{
    while (1) {
        dbg("+++++++++++++++++++++Iterate from First thread++++++++++++++++++++");
        for (ah->thread_iter = 0 ; ah->thread_iter  < ah->thread_index; ah->thread_iter++)
        {
            int r = 0;
            async_thread_t *at = &ah->threads[ah->thread_iter];
            tdbg(at, "Trying to executed async thread");
            switch (at->tstatus) {
                case THREAD_STATUS_INIT:
                    if (!at->func)
                        continue;
                    if (at->tstatus == THREAD_STATUS_INIT)
                        swapcontext(&at->bcxt, &at->this);
                break;

                case THREAD_STATUS_BLOCKED:
                    at->tstatus = THREAD_STATUS_READY;
                    r = getcontext(&at->bcxt);
                    if (r) {
                        perror("getcontext");
                        terr(at, "error getting context");
                        return -1;
                    }
                    if (at->tstatus == THREAD_STATUS_READY) {
                        r = setcontext(&at->this);
                        if (r) {
                            perror("setcontext");
                            terr(at, "error setting context");
                            return -1;
                        }
                    }
                break;
            }
        }
        sleep(5);
        dbg("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    }
}

typedef struct test {
    int testid;
} test_t;

int async_read(async_thread_t *at, test_t *t)
{
    sleep(t->testid);
    tdbg(at, "Calling Yield testid %d", t->testid);
    async_yield(at);
    tdbg(at, "Returning from yield testid %d", t->testid);
}

static void test_th1(void *a1, void *a2)
{
    async_thread_t *at = a1;
    test_t *t = a2;
    tdbg(at, "++ testid %d", t->testid);
    unsigned int loop_count = 0;
    while (1)
    {
        for (int i = 0 ; i < t->testid; i++)
        {
            sleep(1);
            async_yield(at);
        }
        loop_count++;
        tdbg(at, "Completed loop %d times. testid %d",loop_count, t->testid);
    }
    tdbg(at, "-- testid %d", t->testid);
}

int main(int argc, char **argv)
{
    int ret = 0;
    async_handle_t ah;
    async_thread_t *at = NULL;
    test_t t1 = {.testid = 5}, t2 = {.testid = 7};

    ret = async_init(&ah);
    assert(ret == 0);
    ret = async_thread(&ah, &at, test_th1, &t1, "test1");
    assert(ret == 0);

    ret = async_thread(&ah, &at, test_th1, &t2, "test7");
    assert(ret == 0);
    async_run(&ah);
}

