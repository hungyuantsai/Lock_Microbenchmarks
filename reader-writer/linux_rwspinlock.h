#include "all.h"


/* GNU rwlock */
static atomic_llong cnt = 10000;
static atomic_llong max = 10000;
void rwlock_init();
void rwlock_writer_lock();
void rwlock_reader_lock();
void rwlock_writer_unlock();
void rwlock_reader_unlock();


/* 會用到的函數 */
long timespec2nano();

void reader_thread();
void wirter_thread();

void thread();

void printResult();