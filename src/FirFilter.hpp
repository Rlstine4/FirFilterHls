#pragma once

#include <ap_fixed.h>
#include <hls_stream.h>
#include "CircularBuffer.hpp"

#ifndef NUM_TAPS
#define NUM_TAPS 8
#endif

#ifndef SSR
#define SSR 4
#endif

#ifndef NUM_BEATS
#define NUM_BEATS 128
#endif

using coeff_t = ap_fixed<16, 2>;
using sample_val_t = ap_fixed<16, 2>;
using acc_val_t = ap_fixed<32, 6>;

// Complex baseband sample: 16-bit I (real) + 16-bit Q (imag).
struct ComplexSample {
    sample_val_t real;
    sample_val_t imag;
};

// One AXI4-Stream beat: SSR complex samples, bus width = 32 * SSR bits.
//   SSR=1 -> 32 bit, SSR=2 -> 64 bit, SSR=4 -> 128 bit, SSR=8 -> 256 bit
struct SamplePacket {
    ComplexSample samples[SSR];
};

// AXI4-Lite shadow register map.
// Driver protocol:
//   1. write taps[0 .. NUM_TAPS-1]
//   2. toggle write_config                  <- a toggle commits the new tap set
struct FirConfig {
    coeff_t taps[NUM_TAPS];
    bool write_config;
};

// Committed coefficient set used by the datapath. Kept separate from the
// AXI4-Lite shadow registers above so in-flight samples are never filtered
// with a half-written coefficient set.
struct ActiveConfig {
    coeff_t taps[NUM_TAPS];
    bool write_config;
};

// Copy the whole tap set out of the AXI4-Lite shadow registers in one cycle.
void save_config(const FirConfig& axi_cfg, ActiveConfig& active_cfg);

// True when the processor has toggled write_config, i.e. committed a new set.
bool config_toggled(const FirConfig& axi_cfg, const ActiveConfig& active_cfg);

// Datapath: one beat in, one beat out. No knowledge of frame or packet counts.
class FirFilterCore {
private:
    CircularBuffer<ComplexSample, NUM_TAPS> cb_;

public:
    FirFilterCore();
    SamplePacket process(const SamplePacket& in_pkt, const ActiveConfig& cfg);
};

// Top-level streaming module: free-running, one beat per clock.
void FirFilter(hls::stream<SamplePacket>& in_stream,
               hls::stream<SamplePacket>& out_stream,
               const FirConfig& axi_cfg);
