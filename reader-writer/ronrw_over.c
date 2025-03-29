#include "ronrw_over.h"

/* 函式 */
long timespec2nano(struct timespec ts) {
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void routing2idCov() {
    for (int core = 0; core < Num_Core; core++) {
        cpu_order[routing[core]] = core;
    }
}

void rwlock_init(rwlock_t *rw_lock) {
    rw_lock->w_owner = -1;
    for (int i = 0; i < Num_Core; i++) {
        rw_lock->rwArray[i].waiting_type = 0;
        rw_lock->rwArray[i].reader_in_cs = 0;
        rw_lock->rwArray[i].cs_state = no_wthread_in_cs;
    }
}

/*
    writer lock
    writer 有兩種方法進入 CS 中：
        1.「爭奪」，當 CS 中沒有其他 thread 時，所有 writer 爭奪 global lock，也就是進入 CS 的權力
        2.「等待」，等 CS 中目前正持有 global lock 的 writer 按照順序輪到自己被准許自己進入 CS 中
*/
void rwlock_wrlock(rwlock_t *rw_lock) {
}

/*
    reader lock
    reader 有兩種方法進入 CS 中：
        1.「判斷」，判斷 global lock 是否正被 writer 持有，若無則可直接進入 CS 中
        2.「等待」，等待 CS 中目前正持有 global lock 的 writer 按照順序輪到自己被准許自己進入 CS 中
*/
void rwlock_rdlock(rwlock_t *rw_lock) {
}

/*
    reader & writer unlock
*/
void rwlock_unlock(rwlock_t *rw_lock) {
}

rwlock_t rw;

void reader_thread(rwlock_t *rw) {

    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */
	rwlock_rdlock(rw);

    clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i = 0; i < switchSize; i++) {
        local_reader[i] = global_writer[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    
	rwlock_unlock(rw);


    /* Count */
	readerCount++;
    reader_timeSpend_cs += timespec2nano(ts2) - timespec2nano(ts1);


	/* Remainder Section */
    double var = remanderSectionSize / 200 * 3;
    int lim = (int)(drand48() * var - (var/2) + remanderSectionSize);
    struct timespec loop_begin, loop_current;
    clock_gettime(CLOCK_MONOTONIC, &loop_begin);
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &loop_current);
        if (timespec2nano(loop_current) - timespec2nano(loop_begin) > lim)
            break;
    }
}

void writer_thread(rwlock_t *rw) { 

    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */  
	rwlock_wrlock(rw);

    clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i=0; i< switchSize; i++) 
		global_writer[i] += 1;
    clock_gettime(CLOCK_MONOTONIC, &ts2);

	rwlock_unlock(rw);
    

    /* Count */
    writerCount++;
    writer_timeSpend_cs += timespec2nano(ts2) - timespec2nano(ts1);


	/* Remainder Section */        
    double var = remanderSectionSize / 10 * 3;
    int lim = (int)(drand48() * var - (var/2) + remanderSectionSize);
    struct timespec loop_begin, loop_current;
    clock_gettime(CLOCK_MONOTONIC, &loop_begin);
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &loop_current);
        if (timespec2nano(loop_current) - timespec2nano(loop_begin) > lim)
            break;
    }
}

atomic_int stop = 0, thread_counter = 0;
void thread() {
    int num = atomic_fetch_add_explicit(&thread_counter, 1, memory_order_release) % Num_Core;
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(num, &cpuset);
	sched_setaffinity(gettid(), sizeof(cpuset), &cpuset);

	order = cpu_order[num];
    
    while (stop == 0) {
        int randomnumber = rand_r(&num) % 100;
        if (randomnumber < writer_ratio) {
            writer_thread(&rw);
        } else {
            reader_thread(&rw);
        }
    }
    atomic_fetch_add_explicit(&Reader_Count, readerCount, memory_order_acq_rel);
    atomic_fetch_add_explicit(&Writer_Count, writerCount, memory_order_acq_rel);
    atomic_fetch_add_explicit(&reader_global_timeSpend_cs, reader_timeSpend_cs, memory_order_acq_rel);
    atomic_fetch_add_explicit(&writer_global_timeSpend_cs, writer_timeSpend_cs, memory_order_acq_rel);
}

void printResult(int signum) {
    static int run = 0;
    run++;
    if (run == Run_Seconds + 1) {
        atomic_store_explicit(&stop, 1, memory_order_release);
        sleep(1);

        printf("RON_RW,\n");
        printf("RS, %d\n", remanderSectionSize);
        printf("W_Count, %.3lf\n", (double)Writer_Count);
        printf("R_Count, %.3lf\n", (double)Reader_Count);
        printf("W_switch, %.3lf\n", ((double)writer_global_timeSpend_cs/Writer_Count));
        printf("R_switch, %.3lf\n", ((double)reader_global_timeSpend_cs/Reader_Count));
        printf("END\n");
        exit(0);
    }
    alarm(1);
}

int main(int argc, char **argv) {

    /* define argv parameters */
    if (argc < 2) {
		printf("usage: <number of threads> <nCS_size> <switchSize (100)> \n");
		return 0;
	}
    threadNum = atoi(argv[1]);
    remanderSectionSize = atoi(argv[2]);
	if (argc >= 4) {
		switchSize=atoi(argv[3]);
	}


    /* init */
    rwlock_init(&rw);
    routing2idCov();


    /* thread create */
    pthread_t* tid = (pthread_t*)malloc(sizeof(pthread_t) * threadNum);

    alarm(1);
    signal(SIGALRM, printResult);
    
    for (long i = 0; i < threadNum; i++)
        pthread_create(&tid[i], NULL ,(void *) thread, (void*)i);

    for (int i = 0; i < threadNum; i++)
	    pthread_join(tid[i], NULL);

    return 0;
}
