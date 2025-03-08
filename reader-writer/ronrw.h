#include "all.h"


/* RON rwlock Definition */
#define unlikely(x) __builtin_expect(!!(x), 0)

#define no_wthread_in_cs -1

#define wthread_apply 1         // if (w_state == 1) 代表 writer 想要進入 CS 內
#define wthread_approve 0       // if (w_state == 0) 代表 writer 已經離開 CS 了

#define rthread_approve 2       // if (r_state == 2) 代表 reader 被允許可以進入 CS 內
#define rthread_apply 1         // if (r_state == 1) 代表 reader 想要進入 CS 內
#define rthread_leave 0         // if (r_state == 0) 代表 reader 已經離開 CS 了


/* RON rwlock Structure */
typedef struct WaitingNode {
        atomic_int w_state;
        atomic_int r_state;
        char padding[8]; // 讓同一個 ccx 的 core 共享同一個 cache line
} WaitingNode __attribute__ ((aligned (8)));

typedef struct rwlock_t {
    volatile atomic_int w_owner;                // 目前哪個 TSP 的 writer 在 CS 中，例如：（w_owner == 3）代表 order 為 3 的核心在 CS 中
    volatile atomic_int rthread_in_cs_count;    // 計算目前 CS 中 reader 的數量
    WaitingNode rwArray[128];    
} rwlock_t __attribute__ ((aligned (64)));

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