#include "oron.hpp"

/* 函式 */
long timespec2nano(struct timespec ts) {
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void routing2idCov() {
    for (int core = 0; core < Num_Core; core++) {
        cpu_order[routing[core]] = core;
    }
}

oron_lock::ORONLock *latch = new oron_lock::ORONLock;

bool reader_thread() {
    
    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */
    bool restart = false;
    uint64_t v = latch->try_begin_read(restart);
    if (restart) {
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i = 0; i < switchSize; i++) {
        local_reader[i] = global_writer[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &ts2);


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

    return latch->validate_read(v); 
}

void writer_thread() { 

    struct timespec ts1 = {0}, ts2 = {0};

    /* Critical Section */  
	latch->lock(order);

    clock_gettime(CLOCK_MONOTONIC, &ts1);
	for (int i=0; i< switchSize; i++)
		global_writer[i] += 1;
    clock_gettime(CLOCK_MONOTONIC, &ts2);

	latch->unlock(order);


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

std::atomic<int> stop(0), thread_counter(0);
void *thread(void *) {
    unsigned int num = atomic_fetch_add_explicit(&thread_counter, 1, std::memory_order_release) % Num_Core;
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(num, &cpuset);
	sched_setaffinity(gettid(), sizeof(cpuset), &cpuset);

    order = cpu_order[num];
    
    while (stop == 0) {
        int randomnumber = rand_r(&num) % 100;
        if (randomnumber < writer_ratio) {
            writer_thread();
        } else {
            reader_thread();
        }
    }
    
    atomic_fetch_add_explicit(&Reader_Count, readerCount, std::memory_order_acq_rel);
    atomic_fetch_add_explicit(&Writer_Count, writerCount, std::memory_order_acq_rel);   
    atomic_fetch_add_explicit(&reader_global_timeSpend_cs, reader_timeSpend_cs, std::memory_order_acq_rel);
    atomic_fetch_add_explicit(&writer_global_timeSpend_cs, writer_timeSpend_cs, std::memory_order_acq_rel);
    
    return NULL;
}

void printResult(int signum) {
    static int run = 0;
    run++;
    if (run == Run_Seconds + 1) {
        stop.store(1, std::memory_order_release);
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
    routing2idCov();


    /* thread create */
    pthread_t* tid = (pthread_t*)malloc(sizeof(pthread_t) * threadNum);

    alarm(1);
    signal(SIGALRM, printResult);
    
    for (long i = 0; i < threadNum; i++)
        pthread_create(&tid[i], NULL, thread, (void*)i);

    for (int i = 0; i < threadNum; i++)
	    pthread_join(tid[i], NULL);

    return 0;
}
