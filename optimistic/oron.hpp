#include "all.hpp"

#define Num_core 128

typedef struct alignas(8) optlock_t{  
    std::atomic<int> wait_ary[Num_core];
    std::atomic<uint64_t> version; 
    optlock_t() : version(0), wait_ary{} {}
    ~optlock_t(){}
} optlock_t;

namespace oron_lock {

class ORONLock {

public:
    static constexpr uint64_t kLockedBit = 1ull << 63;
    static constexpr uint64_t kInvalidVersion = 0;
    static constexpr uint64_t kVersionStride = 1;
    static constexpr uint64_t kNextUnlockedVersion = kVersionStride - kLockedBit;

    template <typename T>
    static inline bool has_locked_bit(const T val) {
        return (reinterpret_cast<uint64_t>(val) & kLockedBit);
    }

    static inline bool is_version(const uint64_t ptr) { return !has_locked_bit(ptr); }

    static inline uint64_t make_locked_version(const uint64_t v) {
        assert(!has_locked_bit(v));
        return v | kLockedBit;
    }

private:
    optlock_t *optlock;

public:
    ORONLock() { optlock = new optlock_t; }
    ~ORONLock() { delete optlock; }

    uint64_t lock(int order) {
        uint64_t versioncurr, versionlock;
        atomic_store_explicit(&(optlock->wait_ary[order]), 1, std::memory_order_release);
        while (1) {
            while (optlock->wait_ary[order] != 0 && ORONLock::has_locked_bit(&optlock->version)) {
                asm("pause");
            }
            versioncurr = atomic_load_explicit(&optlock->version, std::memory_order_acquire);
            if (!ORONLock::has_locked_bit(versioncurr)) {
                versionlock = ORONLock::make_locked_version(versioncurr);
                if (atomic_compare_exchange_weak_explicit(&(optlock->version), &versioncurr, versionlock, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    optlock->wait_ary[order] = 0;
                    atomic_thread_fence(std::memory_order_release); // 避免使用者 lock() 完馬上呼叫 unlock()
                    return versioncurr;
                }
                continue;
            }
            if (atomic_load_explicit(&(optlock->wait_ary[order]), std::memory_order_acquire) == 0) {
                //printf("lock: %d\n", order);
                return versioncurr - kLockedBit;
            }
        }
    }

    void unlock(int order) {
        for (int i = 1; i < Num_core; i++) {
            __builtin_prefetch((int*)&optlock->wait_ary[(order+i+2)%Num_core], 0, 0);
            if ((optlock->wait_ary[(order+i)%Num_core] == 1)) {
                atomic_store_explicit(&(optlock->wait_ary[(order+i)%Num_core]), 0, std::memory_order_relaxed);
                //printf("lock: %d\n", order);
                return;
            }
        }
        optlock->version.fetch_add(kNextUnlockedVersion);
        return;
    }

    bool try_lock(uint64_t version, int order) {
        if (!validate_read(version)) {
            return false;
        }
        
        atomic_store_explicit(&(optlock->wait_ary[order]), 1, std::memory_order_release);
        
        uint64_t versioncurr = version;
        uint64_t versionlock = ORONLock::make_locked_version(versioncurr);
        for (int i=0; i<100; i++) {
            if (!ORONLock::has_locked_bit(versioncurr)) {
                optlock->wait_ary[order] = 0;
                atomic_thread_fence(std::memory_order_release);
                return atomic_compare_exchange_weak_explicit(&(optlock->version), &versioncurr, versionlock, std::memory_order_relaxed, std::memory_order_relaxed);
            }
            if (atomic_load_explicit(&(optlock->wait_ary[order]), std::memory_order_acquire) == 0) {
                return 1;
            }
        }
        int one = 1;
        return !atomic_compare_exchange_weak_explicit(&(optlock->wait_ary[order]), &one, 0, std::memory_order_release, std::memory_order_acquire);
    }

    uint64_t try_begin_read(bool &restart) const {
        uint64_t version = atomic_load_explicit(&optlock->version, std::memory_order_acquire);
        restart = ORONLock::has_locked_bit(version);
        // If [restart] hasn't been changed to false, [version] is guaranteed to be a version.
        return version;
    }

    bool validate_read(uint64_t version) const { //（免改
        uint64_t v = atomic_load_explicit(&optlock->version, std::memory_order_acquire);
        return version == v;
    }
};

}