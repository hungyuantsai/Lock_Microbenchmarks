#include "pthread_rwspinlock.h"


/* 函式 */
long timespec2nano(struct timespec ts) {
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

pthread_rwlock_t rw;

void reader_thread(pthread_rwlock_t *rw) {

    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */
	pthread_rwlock_rdlock(rw);

	clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i = 0; i < switchSize; i++) {
        local_reader[i] = global_writer[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    
	pthread_rwlock_unlock(rw);


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

void writer_thread(pthread_rwlock_t *rw) {

    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */  
	pthread_rwlock_wrlock(rw);

	clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i=0; i< switchSize; i++) 
		global_writer[i] += 1;
    clock_gettime(CLOCK_MONOTONIC, &ts2);

	pthread_rwlock_unlock(rw);
    

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
    pthread_rwlock_init(&rw, NULL);


    /* thread create */
    pthread_t* tid = (pthread_t*)malloc(sizeof(pthread_t) * threadNum);

    alarm(1);
    signal(SIGALRM, printResult);
    
    for (long i=0; i<threadNum; i++)
        pthread_create(&tid[i], NULL ,(void *) thread, (void*)i);

    for (int i=0; i<threadNum; i++)
	    pthread_join(tid[i], NULL);

    return 0;
}
