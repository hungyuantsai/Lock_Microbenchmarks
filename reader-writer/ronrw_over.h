#include "all.h"


/* RON rwlock Definition */
#define unlikely(x) __builtin_expect(!!(x), 0)

#define no_wthread_in_cs -1

#define writer 1        // if (waiting_type == 1) 想進入 CS 的為 writer
#define reader 2        // if (waiting_type == 2) 想進入 CS 的為 reader

#define cs_empty_not 0  // if (cs_state == 0) 這 core 上有人進入 CS
#define writer_allow 1  // if (cs_state == 1) 當 writer 可以進入
#define reader_allow 2  // if (cs_state == 2) 當 reader 可以進入


/* RON rwlock Structure */
typedef struct{
    atomic_int waiting_type;
    atomic_int reader_in_cs;
    atomic_int cs_state;
    char padding[112];
} WaitingNode __attribute__ ((aligned (64)));

typedef struct {   
    WaitingNode rwArray[128];
    volatile atomic_int w_owner;
} rwlock_t __attribute__ ((aligned (128)));

void rwlock_init();
void rwlock_wrlock();
void rwlock_rdlock();
void rwlock_unlock();


/* 會用到的函數 */
long timespec2nano();

void routing2idCov();

void reader_thread();
void wirter_thread();

void thread();

void printResult();