#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <sys/syscall.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <immintrin.h>
#include <type_traits>
#include <atomic>
#include <cassert>


#define Num_Core 128
#define Run_Seconds 10
#define writer_ratio 10


/* TSP order */
static int routing[Num_Core] = {0,1,2,3,64,65,66,67,4,5,6,7,68,69,70,71,8,9,10,11,72,73,74,75,
                                12,13,14,15,76,77,78,79,40,41,42,43,104,105,106,107,36,37,38,39,
                                100,101,102,103,44,45,46,47,108,109,110,111,96,97,98,99,32,33,34,35,
                                56,57,58,59,120,121,122,123,52,53,54,55,116,117,118,119,60,61,62,63,
                                124,125,126,127,112,113,114,115,48,49,50,51,28,29,30,31,92,93,94,95,
                                80,81,82,83,16,17,18,19,20,21,22,23,84,85,86,87,88,89,90,91,24,25,26,27};


/* Reader & Writer 在 CS 中的行為所使用的陣列 */
int global_writer[1024];
__thread int local_reader[100];


/* TSP order（放在這比較快） */
static __thread int order;
static int cpu_order[Num_Core];


/* 初始輸入三個參數 */
int threadNum = 0, remanderSectionSize = 0, switchSize = 100;


/* 計數 */
std::atomic<long long> Writer_Count(0);
std::atomic<long long> Reader_Count(0);
std::atomic<long long> reader_global_timeSpend_cs(0);
std::atomic<long long> writer_global_timeSpend_cs(0);

__thread long long readerCount = 0;
__thread long long writerCount = 0;
__thread long long reader_timeSpend_cs = 0;
__thread long long writer_timeSpend_cs = 0;