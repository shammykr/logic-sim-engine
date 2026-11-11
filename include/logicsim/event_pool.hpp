// Fixed-size object pool for simulation events.
//
// Events are created and destroyed tens of millions of times per run. The pool allocates them in
// chunks of kChunk objects and recycles released events through a free list, so steady-state
// simulation does no heap allocation at all.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace logicsim {

template <typename T, std::size_t kChunk = 4096>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t prealloc = 0) {
        while (capacity() < prealloc) grow();
    }

    T* acquire() {
        if (free_.empty()) grow();
        T* p = free_.back();
        free_.pop_back();
        return p;
    }

    void release(T* p) { free_.push_back(p); }

    std::size_t capacity() const { return chunks_.size() * kChunk; }
    std::size_t chunks() const { return chunks_.size(); }
    std::size_t in_use() const { return capacity() - free_.size(); }

private:
    void grow() {
        chunks_.push_back(std::make_unique<T[]>(kChunk));
        T* base = chunks_.back().get();
        free_.reserve(capacity());
        for (std::size_t i = kChunk; i-- > 0;) free_.push_back(base + i);
    }

    std::vector<std::unique_ptr<T[]>> chunks_;
    std::vector<T*> free_;
};

}  // namespace logicsim
