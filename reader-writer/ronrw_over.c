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
    
    /* 當 writer 想進入 CS 所以把 waiting_type 設為 writer */
    atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), writer, memory_order_release);
    while(1) {
        
        /* 目前 CS 不為空，或是，目前 reader 正在 CS 當中 */
        /* 代表有相同核心上的 thread 在 CS 中則使用 sched_yield() 禮讓 */
        if (rw_lock->rwArray[order].cs_state == cs_empty_not || rw_lock->rwArray[order].cs_state == reader_allow) {
            sched_yield();
        }

        /*「等待」，等待正在 CS 中的 writer 准許自己進入 CS 中 */
        int require = 1; 
        if (rw_lock->rwArray[order].cs_state == require){
            require = 1;
            if (atomic_compare_exchange_weak(&(rw_lock->rwArray[order].cs_state), &require, 0)) {
                break;
            }
        }

        /* 這裡是 writer 重新要求進入 CS 因為 waiting_type 可能被同核心之 reader thread 設為 reader */
        atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), writer, memory_order_release);

        /*「爭奪」，plock，預先判斷若 w_owner == no_wthread_in_cs 代表沒有 writer 在 CS 中，所以 writer 可以進行 global lock（w_owner）爭奪 */
	    if (__glibc_unlikely(rw_lock->w_owner == no_wthread_in_cs) && rw_lock->rwArray[order].cs_state != reader_allow) {
		    int flag = -1;
    		if (atomic_compare_exchange_weak(&(rw_lock->w_owner), &flag, order)) {
                
                /* 代表 CS 還有人 */
                atomic_store_explicit(&(rw_lock->rwArray[order].cs_state), cs_empty_not, memory_order_release);
       			
                /* 等待 CS 中的 reader 出來 */
                int cnt;
                for (int i=1; i<Num_Core; i++) {
                    // while ((atomic_load_explicit(&(rw_lock->rwArray[(order+i) % Num_Core].reader_in_cs), memory_order_acquire) > 0)) {
                    //     asm("pause");
                    // }
                    if ((cnt = atomic_load_explicit(&(rw_lock->rwArray[(order+i) % Num_Core].reader_in_cs), memory_order_acquire)) == 0) continue;
                    while (cnt == rw_lock->rwArray[(order+i) % Num_Core].reader_in_cs) {
                        asm("pause");
                    }   
       			}
                atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), 0, memory_order_release);
        	  	return;       			    		   
    		} 
    	}
    }

    // 計算要等的 reader 數量
    // example (1)->2->3->(4) 1, 4 為 writer，2, 3 為 reader，1 現在持有鎖 rw_lock->w_owner＝1, order=4, count = 4 - 1 = 3
    // example (3)->0->1->(2) 3, 2 為 writer，0, 1 為 reader，3 現在持有鎖 rw_lock->w_owner＝3, order=2, count = cpu數量（4）- 3 + 2 = 3
    int last = rw_lock->w_owner;
    int count = 0;
    if (order > last)
    	count = order - last;
    else 
    	count = Num_Core - last + order;

    /* 等待 CS 中的 reader 出來 */
    int cnt;
    for (int j=1; j<count; j++) {
        // while ((atomic_load_explicit(&(rw_lock->rwArray[(last+j) % Num_Core].reader_in_cs), memory_order_acquire) > 0)) {
        //     asm("pause");
        // }
        if ((cnt = atomic_load_explicit(&(rw_lock->rwArray[(last+j) % Num_Core].reader_in_cs), memory_order_acquire)) == 0) continue;
        while (cnt == rw_lock->rwArray[(last+j) % Num_Core].reader_in_cs) {
            asm("pause");
        }
    }

    /* 自己 order 拿到鎖 */
    atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), 0, memory_order_release); 
    atomic_store_explicit(&(rw_lock->w_owner), order, memory_order_release);                       
    return;
}

/*
    reader lock
    reader 有兩種方法進入 CS 中：
        1.「判斷」，判斷 global lock 是否正被 writer 持有，若無則可直接進入 CS 中
        2.「等待」，等待 CS 中目前正持有 global lock 的 writer 按照順序輪到自己被准許自己進入 CS 中
*/
void rwlock_rdlock(rwlock_t *rw_lock) {
    
    /* 當 reader 想進入 CS 所以把 waiting_type 設為 reader */
    atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), reader, memory_order_release);

    while(1) {

        /* 目前 CS 不為空，或是，目前 writer 正在 CS 當中 */
        /* 代表有相同核心上的 thread 在 CS 中則使用 sched_yield() 禮讓 */
        if (rw_lock->rwArray[order].cs_state == cs_empty_not || rw_lock->rwArray[order].cs_state == writer_allow) {
            sched_yield();
        }

        /*「等待」，等待正在 CS 中的 reader 准許自己進入 CS 中 */
        int require = 2;
        if (rw_lock->rwArray[order].cs_state == require) {
            require = 2;
            if (atomic_compare_exchange_weak(&(rw_lock->rwArray[order].cs_state), &require, 0)) {
                break;
            }
        } 
        
        /* 這裡是 reader 重新要求進入 CS 因為 waiting_type 可能被同核心之 writer thread 設為 writer */
        atomic_store_explicit( &(rw_lock -> rwArray[order].waiting_type), reader, memory_order_release);

        /*「爭奪」，plock，預先判斷若 w_owner == no_wthread_in_cs 代表沒有 writer 在 CS 中，所以 writer 可以進行 global lock（w_owner）爭奪 */
        if (__glibc_unlikely(rw_lock->w_owner == -1) && rw_lock->rwArray[order].cs_state != reader_allow) {

            /* 進入 CS */
            atomic_fetch_add_explicit(&(rw_lock->rwArray[order].reader_in_cs), 1, memory_order_release);          
            if (atomic_load_explicit(&(rw_lock->w_owner), memory_order_acquire) == no_wthread_in_cs) {
                atomic_store_explicit(&(rw_lock->rwArray[order].cs_state), cs_empty_not, memory_order_release);
                atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), 0, memory_order_release);
        	 	return;
    		}
            atomic_fetch_add_explicit(&(rw_lock->rwArray[order].reader_in_cs), -1, memory_order_release);
        }

    }
    atomic_store_explicit(&(rw_lock->rwArray[order].waiting_type), 0, memory_order_release);
    return;
}

/*
    reader & writer unlock
*/
void rwlock_unlock(rwlock_t *rw_lock) {

    // reader unlock
    // 當 reader 在 CS 中時 reader_in_cs 的值必為 rthread_approve，如此能判斷為哪一種 thread
    if ((atomic_load_explicit(&(rw_lock->rwArray[order].reader_in_cs), memory_order_acquire) > 0)) {
        atomic_fetch_add_explicit(&(rw_lock->rwArray[order].reader_in_cs), -1, memory_order_release);          
        return; 
    }


    // writer unlock
    // 依照 TSP order 尋找想要進入 CS 的 thread
    int next = -1;
    for (int i=1; i<Num_Core; i++) {
        next = (order+i) % Num_Core;

        /* 若下一位正在等待的人為 next 並且是 reader，則 waiting_type 為 reader，讓 reader 進入 CS */
        if ((atomic_load_explicit( &(rw_lock->rwArray[next].waiting_type), memory_order_acquire) == reader)) {
            atomic_fetch_add_explicit(&(rw_lock->rwArray[next].reader_in_cs), 1, memory_order_release);          
            atomic_store_explicit(&(rw_lock->rwArray[next].cs_state), reader_allow, memory_order_release);
        }

        /* 若下一位正在等待的人為 next 並且是 writer，則 waiting_type 為 writer，讓 writer 進入 CS */
        else if((atomic_load_explicit(&(rw_lock->rwArray[next].waiting_type), memory_order_acquire) == writer)) {
            atomic_store_explicit(&(rw_lock->rwArray[next].cs_state), writer_allow, memory_order_release);
            return;
        }
    }
    
    atomic_store_explicit(&(rw_lock->w_owner), no_wthread_in_cs, memory_order_release);           
    return;
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
