#pragma once

#include <cstddef>

#ifndef __SYNTHESIS__
#include <cassert>
#define SR_CHECK(cond) assert(cond)
#else
#define SR_CHECK(cond) ((void)0)
#endif

// Fixed-depth shift register.
//
//   - buf_[0] holds the newest element; buf_[N-1] holds the oldest element.
//   - write() shifts existing elements up by 1 and stores the new element at 0.
//   - read(desired_idx) returns buf_[desired_idx], so desired_idx is legal only
//     in [0, N-1].
template <typename T, std::size_t N>
class ShiftRegister {
    static_assert(N >= 1, "ShiftRegister: depth N must be at least 1");
    static_assert(sizeof(T) > 0, "ShiftRegister: element type T must be complete");

private:
    T buf_[N];

public:
    ShiftRegister() : buf_{} {
#pragma HLS ARRAY_PARTITION variable=buf_ complete dim=1
    }

    void write(const T & sample) {
#pragma HLS INLINE
        for (std::size_t i = N - 1; i > 0; --i) {
#pragma HLS UNROLL
            buf_[i] = buf_[i - 1];
        }
        buf_[0] = sample;
    }

    T read(std::size_t desired_idx) const {
#pragma HLS INLINE
        SR_CHECK(desired_idx < N);
        return buf_[desired_idx];
    }
};
