#include "all.hpp"

namespace mcsrw {

// Implementation follows https://www.cs.rochester.edu/research/synchronization/pseudocode/rw.html

#define CACHELINE_SIZE 64

using offset_t = uint64_t;

class MCSRWLock;

enum class MCSRWRequestorClass : uint64_t { READING, WRITING };
enum class MCSRWSuccessorClass : uint32_t { NONE, READER, WRITER };

struct alignas(CACHELINE_SIZE * 2) MCSRWQNode {
 public:
  alignas(CACHELINE_SIZE) std::atomic<MCSRWQNode *> next_;
  alignas(CACHELINE_SIZE) std::atomic<MCSRWRequestorClass> class_;
  alignas(CACHELINE_SIZE * 2) union MCSRWQNodeState {
    std::atomic<uint64_t> state_;
    struct MCSRWQNodeStateBlockAndSuccessorClass {
      std::atomic<uint32_t> blocked_;
      std::atomic<MCSRWSuccessorClass> successor_class_;
    } record_;
  } state_;

  static_assert(sizeof(MCSRWQNodeState) == sizeof(uint64_t),
                "sizeof MCSRWQNodeState is not 8-byte");

  static inline uint64_t make_state(uint32_t blocked, MCSRWSuccessorClass successor_class) {
    // XXX(shiges): endianness
    uint64_t result = static_cast<uint64_t>(successor_class) << 32;
    result |= static_cast<uint64_t>(blocked);
    return result;
  }

  inline void set_next(MCSRWQNode *next) { next_.store(next, std::memory_order_seq_cst); }

  inline void set_class(MCSRWRequestorClass _class) {
    class_.store(_class, std::memory_order_seq_cst);
  }

  inline void set_state(uint32_t blocked, MCSRWSuccessorClass successor_class) {
    uint64_t state = MCSRWQNode::make_state(blocked, successor_class);
    state_.state_.store(state, std::memory_order_seq_cst);
  }

  inline void set_blocked(uint32_t blocked) {
    state_.record_.blocked_.store(blocked, std::memory_order_seq_cst);
  }

  inline void set_successor_class(MCSRWSuccessorClass successor_class) {
    state_.record_.successor_class_.store(successor_class, std::memory_order_seq_cst);
  }

  inline bool compare_and_set_state(uint64_t expected, uint64_t desired) {
    return state_.state_.compare_exchange_strong(expected, desired);
  }

  inline MCSRWQNode *const get_next() const { return next_.load(std::memory_order_acquire); }

  inline const MCSRWRequestorClass get_class() const {
    return class_.load(std::memory_order_acquire);
  }

  inline const uint32_t get_blocked() const {
    return state_.record_.blocked_.load(std::memory_order_acquire);
  }

  inline const MCSRWSuccessorClass get_successor_class() const {
    return state_.record_.successor_class_.load(std::memory_order_acquire);
  }
};

class MCSRWLock {
 public:
  static constexpr uint32_t kFree = 0;
  static constexpr uint32_t kWriterActiveFlag = 0x1;
  static constexpr uint32_t kReaderCountIncr = 0x10;

  static constexpr uint32_t kUnblocked = 0;
  static constexpr uint32_t kBlocked = 1;

  static constexpr offset_t kNullPtr = 0;

  static inline bool is_null(offset_t offset) { return offset == kNullPtr; }

  static inline MCSRWQNode *get_qnode_ptr(offset_t offset) {
    return reinterpret_cast<MCSRWQNode *>(offset);
  }

  static inline offset_t make_qnode_ptr(MCSRWQNode *const ptr) {
    return reinterpret_cast<offset_t>(ptr);
  }

 private:
  std::atomic<uint64_t> u64a_{0};
  std::atomic<uint64_t> u64b_{0};
  std::atomic<uint32_t> u32_{0};

#define tail_ u64a_
#define next_writer_ u64b_
#define reader_count_ u32_

#define write_requests_ u64a_
#define write_completions_ u64b_
#define reader_count_and_flag_ u32_

 public:
  MCSRWLock() = default;

  MCSRWLock(const MCSRWLock &) = delete;
  MCSRWLock &operator=(const MCSRWLock &) = delete;

  inline void lock(MCSRWQNode *qnode) {
    assert(qnode != nullptr);

    qnode->set_class(MCSRWRequestorClass::WRITING);
    qnode->set_state(kBlocked, MCSRWSuccessorClass::NONE);
    qnode->set_next(nullptr);
    offset_t self = MCSRWLock::make_qnode_ptr(qnode);
    offset_t prev = tail_.exchange(self);

    if (MCSRWLock::is_null(prev)) {
      next_writer_.exchange(self);
      if (reader_count_.load(std::memory_order_acquire) == 0) {
        if (next_writer_.exchange(kNullPtr) == self) {
          qnode->set_blocked(kUnblocked);
          return;
        }
      }
    } else {
      MCSRWQNode *pred = MCSRWLock::get_qnode_ptr(prev);
      pred->set_successor_class(MCSRWSuccessorClass::WRITER);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      pred->set_next(qnode);
    }

    while (qnode->get_blocked() == kBlocked) {
    }
  }

  inline void unlock(MCSRWQNode *qnode) {
    assert(qnode != nullptr);

    MCSRWQNode *succ = qnode->get_next();
    if (succ == nullptr) {
      offset_t self = MCSRWLock::make_qnode_ptr(qnode);
      if (tail_.compare_exchange_strong(self, kNullPtr)) {
        return;
      }

      do {
        succ = qnode->get_next();
      } while (succ == nullptr);
    }

    assert(succ != nullptr);

    if (succ->get_class() == MCSRWRequestorClass::READING) {
      reader_count_.fetch_add(1);
    }
    succ->set_blocked(kUnblocked);
  }

  inline void read_lock(MCSRWQNode *qnode) {
    assert(qnode != nullptr);

    qnode->set_class(MCSRWRequestorClass::READING);
    qnode->set_state(kBlocked, MCSRWSuccessorClass::NONE);
    qnode->set_next(nullptr);
    offset_t self = MCSRWLock::make_qnode_ptr(qnode);
    offset_t prev = tail_.exchange(self);

    if (prev == kNullPtr) {
      reader_count_.fetch_add(1);
      qnode->set_blocked(kUnblocked);
    } else {
      MCSRWQNode *pred = MCSRWLock::get_qnode_ptr(prev);
      if (pred->get_class() == MCSRWRequestorClass::WRITING ||
          pred->compare_and_set_state(
              MCSRWQNode::make_state(kBlocked, MCSRWSuccessorClass::NONE),
              MCSRWQNode::make_state(kBlocked, MCSRWSuccessorClass::READER))) {
        pred->set_next(qnode);
        while (qnode->get_blocked() == kBlocked) {
        }
      } else {
        reader_count_.fetch_add(1);
        pred->set_next(qnode);
        qnode->set_blocked(kUnblocked);
      }
    }

    if (qnode->get_successor_class() == MCSRWSuccessorClass::READER) {
      MCSRWQNode *succ = qnode->get_next();
      if (succ == nullptr) {
        do {
          succ = qnode->get_next();
        } while (succ == nullptr);
      }

      reader_count_.fetch_add(1);
      succ->set_blocked(kUnblocked);
    }
  }

  inline void read_unlock(MCSRWQNode *qnode) {
    assert(qnode != nullptr);

    MCSRWQNode *succ = qnode->get_next();
    if (succ == nullptr) {
      offset_t self = MCSRWLock::make_qnode_ptr(qnode);
      if (tail_.compare_exchange_strong(self, kNullPtr)) {
        goto reader_exit;
      }

      do {
        succ = qnode->get_next();
      } while (succ == nullptr);
    }

    assert(succ != nullptr);

    if (qnode->get_successor_class() == MCSRWSuccessorClass::WRITER) {
      offset_t next = MCSRWLock::make_qnode_ptr(succ);
      next_writer_.exchange(next);
    }

  reader_exit:
    offset_t w = kNullPtr;
    if (reader_count_.fetch_sub(1) == 1 &&
        (w = next_writer_.load(std::memory_order_acquire)) != kNullPtr &&
        reader_count_.load(std::memory_order_acquire) == 0 &&
        next_writer_.compare_exchange_strong(w, kNullPtr)) {
      MCSRWQNode *next_writer = MCSRWLock::get_qnode_ptr(w);
      next_writer->set_blocked(kUnblocked);
    }
  }

#undef tail_
#undef next_writer_
#undef reader_count_

#undef write_requests_
#undef write_completions_
#undef reader_count_and_flag_
};

}  // namespace mcsrw


/* 會用到的函數 */
long timespec2nano();

void reader_thread();
void wirter_thread();

void *thread(void *);

void printResult();