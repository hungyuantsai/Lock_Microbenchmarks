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
    }

    void unlock(int order) {
    }

    bool try_lock(uint64_t version, int order) {
    }

    uint64_t try_begin_read(bool &restart) const {
    }

    bool validate_read(uint64_t version) const { //（免改
        uint64_t v = atomic_load_explicit(&optlock->version, std::memory_order_acquire);
        return version == v;
    }
};

}
