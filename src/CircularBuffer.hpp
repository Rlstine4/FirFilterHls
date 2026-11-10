#pragma once

#include <cstddef>

#ifndef __SYNTHESIS__
#include <cassert>
#define CB_CHECK(cond) assert(cond)
#else
#define CB_CHECK(cond) ((void)0)
#endif

// Fixed-capacity circular history buffer.
//
// Index legality:
//   - head_ is always kept in [0, N-1].
//   - read(tap_idx) resolves head_ - tap_idx with a single wrap, so tap_idx is
//     legal only in [0, N-1]. Out-of-range indices are a contract violation and
//     are trapped in C simulation (see CB_CHECK).
template <typename T, int N>
class CircularBuffer {
    static_assert(N >= 1, "CircularBuffer: depth N must be at least 1");
    static_assert(N <= (1 << 20), "CircularBuffer: depth N is unreasonably large");
    static_assert(sizeof(T) > 0, "CircularBuffer: element type T must be complete");

private:
    T buf_[N];
    int head_;

public:
    CircularBuffer() : buf_{}, head_(0) {
#pragma HLS ARRAY_PARTITION variable=buf_ complete dim=1
        CB_CHECK(head_ >= 0 && head_ < N);
    }

    void write(const T& sample) {
#pragma HLS INLINE
        CB_CHECK(head_ >= 0 && head_ < N);
        head_ = (head_ == N - 1) ? 0 : (head_ + 1);
        buf_[head_] = sample;
        CB_CHECK(head_ >= 0 && head_ < N);
    }

    T read(int tap_idx) const {
#pragma HLS INLINE
        CB_CHECK(tap_idx >= 0 && tap_idx < N);
        CB_CHECK(head_ >= 0 && head_ < N);

        int idx = head_ - tap_idx;
        if (idx < 0) {
            idx += N;
        }
        CB_CHECK(idx >= 0 && idx < N);
        return buf_[idx];
    }
};
