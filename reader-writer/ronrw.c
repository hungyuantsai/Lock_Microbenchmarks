#include "ronrw.h"

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
    rw_lock->rthread_in_cs_count = 0;
    for (int i = 0; i < Num_Core; i++) {
        rw_lock->rwArray[i].w_state = 0;
        rw_lock->rwArray[i].r_state = 0;
    }
}

/*
    writer lock
    writer 有兩種方法進入 CS 中：
        1.「爭奪」，當 CS 中沒有其他 thread 時，所有 writer 爭奪 global lock，也就是進入 CS 的權力
        2.「等待」，等 CS 中目前正持有 global lock 的 writer 按照順序輪到自己被准許自己進入 CS 中
*/
void rwlock_wrlock(rwlock_t *rw_lock) {
    
    // 將 w_state = wthread_apply 是為了讓其他人曉得 writer 想要進入 CS 
    atomic_store_explicit(&(rw_lock->rwArray[order].w_state), wthread_apply, memory_order_release);
    
    int flag = -1;
    while (1) {

        // 若（w_state == wthread_apply）&&（w_owner ！= no_wthread_in_cs）則 pause;
        // 代表（當下 writer 想進入 CS）且（有人在 CS）則 writer 等待
        while ((atomic_load_explicit(&(rw_lock->rwArray[order].w_state),memory_order_acquire) == wthread_apply) && (atomic_load_explicit(&(rw_lock->w_owner), memory_order_acquire) != no_wthread_in_cs))
            asm("pause");

        //「等待」，等待正在 CS 中的 writer 准許自己進入 CS 中
        if (atomic_load_explicit(&(rw_lock->rwArray[order].w_state), memory_order_acquire) == wthread_approve) {
            
            // 計算要等的 reader 數量
            // example (1)->2->3->(4) 1, 4 為 writer，2, 3 為 reader，1 現在持有鎖 rw_lock->w_owner＝1, order=4, count = 4 - 1 = 3
            // example (3)->0->1->(2) 3, 2 為 writer，0, 1 為 reader，3 現在持有鎖 rw_lock->w_owner＝3, order=2, count = cpu數量（4）- 3 + 2 = 3
            int last = rw_lock->w_owner;
            int count = 0;
            if (order > last)
                count = order - last;
            else 
                count = Num_Core - last + order;
            
            // 等待 CS 中的 reader 離開 CS
            for (int j=1; j<count; j++) {
        		while ((atomic_load_explicit(&(rw_lock->rwArray[(last+j) % Num_Core].r_state), memory_order_acquire) == rthread_approve)) {
        	       	asm("pause");
        		}         
       	    }

            // 將 w_owner = 自己的 order，代表 "order" 進入 CS
            atomic_store_explicit(&(rw_lock->w_owner), order, memory_order_release); 
            return;
        }

        flag = -1;

        //「爭奪」，plock，預先判斷若 w_owner == no_wthread_in_cs 代表沒有 writer 在 CS 中，所以 writer 可以進行 global lock（w_owner）爭奪
        if (__glibc_unlikely(rw_lock->w_owner == no_wthread_in_cs)) {

            // 爭奪 gloalb lock（w_owner），一次只會有一個 writer 搶到，並在等待 reader 離開 CS 後把 w_owner 設為搶到者的 TSP_ID
            if (atomic_compare_exchange_weak_explicit(&(rw_lock->w_owner), &flag, order, memory_order_release, memory_order_acquire)) {
                
                // 等待先前拿到 global lock 的 reader 更改 r_state，如何確保 r_state 更改？
                // 方法：當 rthread_in_cs_count 為零，則能確保進入 CS 的 reader 已經更改 r_state
                while ((atomic_load_explicit(&(rw_lock->rthread_in_cs_count), memory_order_acquire)) != 0) {
                    asm("pause");
                }

                // 等待 CS 中的 reader 離開 CS
                for (int i = 1; i < Num_Core; i++) {
        			while ((atomic_load_explicit(&(rw_lock->rwArray[(order+i) % Num_Core].r_state), memory_order_acquire) == rthread_approve)) {
         		    	asm("pause");
        		    }         
       			}

                // 將 w_state 改為 wthread_approve 是為了避免，讓其他人誤會自己想要進入 CS 中，因為此時自己已在 CS 中
                atomic_store_explicit( &(rw_lock->rwArray[order].w_state), wthread_approve, memory_order_release);
                return;
            }
        }
    }
    return;
}

/*
    reader lock
    reader 有兩種方法進入 CS 中：
        1.「判斷」，判斷 global lock 是否正被 writer 持有，若無則可直接進入 CS 中
        2.「等待」，等待 CS 中目前正持有 global lock 的 writer 按照順序輪到自己被准許自己進入 CS 中
*/
void rwlock_rdlock(rwlock_t *rw_lock) {
    
    // 將 r_state = rthread_apply 代表讓其他人曉得 reader 想要進入 CS
    atomic_store_explicit(&(rw_lock->rwArray[order].r_state), rthread_apply, memory_order_release);
   
    int flag = -1;
    while (1) {
        
        // 若（r_state == rthread_apply）&&（r_owner ！= no_wthread_in_cs）pause;
        // 代表（當下 reader 想進入 CS）且（有人在 CS）則 reader 等待
        while ((atomic_load_explicit(&(rw_lock->rwArray[order].r_state),memory_order_acquire) == rthread_apply) && (atomic_load_explicit(&(rw_lock->w_owner), memory_order_acquire) != no_wthread_in_cs))        
            asm("pause");
        
        //「等待」，等待正在 CS 中的 writer 准許自己進入 CS 中
        if (atomic_load_explicit(&(rw_lock->rwArray[order].r_state), memory_order_acquire) ==rthread_approve) {
        	return ;
        }

        flag = -1;
        
        //「判斷」，預先判斷 global lock 是否正被 writer 持有，若無則可直接進入 CS 中
        if (__glibc_unlikely(rw_lock->w_owner == no_wthread_in_cs)) {

            // 拿到鎖之前「先告知」多一人：可能有 reader 會進入 CS 中，因為 reader 可能拿鎖失敗！
            atomic_fetch_add_explicit(&rw_lock->rthread_in_cs_count, 1, memory_order_relaxed);
            
            // if (atomic_compare_exchange_weak_explicit(&(rw_lock->w_owner), &flag, -1, memory_order_release,memory_order_acquire))

            // 再次確認 w_owner == no_wthread_in_cs，用來確保真的沒有 writer 在 CS 中
            if (atomic_load_explicit(&(rw_lock->w_owner), memory_order_acquire) == no_wthread_in_cs) {
                
                // 將 r_state 改為 rthread_approve 是為了避免，讓其他人誤會自己想要進入 CS 中，因為此時自己已在 CS 中
                atomic_store_explicit( &(rw_lock->rwArray[order].r_state), rthread_approve, memory_order_release);
                
                // printf("-1 %d\n ",order);

                // 拿到鎖之後，把剛剛的「先告知」的多一人 rthread_in_cs_count 的值回復
                atomic_fetch_add_explicit(&rw_lock->rthread_in_cs_count, -1, memory_order_relaxed); // +（-1）
                return;
            }

            // 拿鎖失敗後，把剛剛的「先告知」的多一人 rthread_in_cs_count 的值回復
            atomic_fetch_add_explicit(&rw_lock->rthread_in_cs_count, -1, memory_order_relaxed); // +（-1）
        }
    }
    return;
}

/*
    reader & writer unlock
*/
void rwlock_unlock(rwlock_t *rw_lock) {

    // reader unlock
    // 當 reader 在 CS 中時 r_state 的值必為 rthread_approve，如此能判斷為哪一種 thread
    if (rw_lock->rwArray[order].r_state == rthread_approve) {

        // 更改 r_state 使之離開 CS
        atomic_store_explicit(&(rw_lock->rwArray[order].r_state), rthread_leave, memory_order_release);
        return;
    }

    // writer unlock
    // 依照 TSP order 尋找想要進入 CS 的 thread
    int one = 1, next;
    for (int i = 1; i < Num_Core; i++) {
        one = 1;
        next = (order+i) % Num_Core;
        // 例：TSP order = A(w) -> B(r) -> C(r) -> D(r) -> E(w)
        // 1. 現 CS 中為 A 且 TSP order 中下一個想進入的 writer 為 E
        // 2. 則 A 透過 TSP order 嘗試尋找下一個 wthread_apply 之 writer（e.g. E）
        // 3. 第二條 if 成立：尋找的途中找到 B, C, D 之 r_state == rthread_apply，故 A 將 B, C, D 之 r_state = rthread_approve，所以 B, C, D 被 A 准許進入 CS 中
        // 4. 第一條 if 成立：而後找到 E 之 w_state == wthread_apply，故 A 將 E 之 w_state = wthread_approve，所以 E 被 A 准許進入 CS 中

        // 對應 writer lock，33 行（if (atomic_load_explicit(&(rw_lock->rwArray[order].w_state), memory_order_acquire) == wthread_approve)）
        // 照順序找到想進入 CS 的 writer 後准許其進入 CS 中
        if ((atomic_load_explicit(&(rw_lock->rwArray[next].w_state), memory_order_acquire) == wthread_apply)) {
            atomic_store_explicit(&(rw_lock->rwArray[next].w_state), wthread_approve, memory_order_release);
            return;
        }

        // 對應 reader lock，109 行（if (atomic_load_explicit(&(rw_lock->rwArray[order].r_state), memory_order_acquire) == rthread_approve)）
        // 讓照順序找到下一個 writer 之前的 reader 進入 CS 中
        if (atomic_compare_exchange_weak_explicit(&(rw_lock->rwArray[next].r_state), &one, rthread_approve,memory_order_release,memory_order_acquire)) {
        }
    }

    // 沒有找到任何 writer 想要進入 CS 中，則打開 global lock，將 w_owner = no_wthread_in_cs
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
