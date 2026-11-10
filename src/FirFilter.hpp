#pragma once

#include <ap_fixed.h>
#include <hls_stream.h>
#include "ShiftRegister.hpp"

#ifndef NUM_TAPS
#define NUM_TAPS 6
#endif

#ifndef NUM_SAMPLES_PER_PACKET
#define NUM_SAMPLES_PER_PACKET 4
#endif

using coeff_t = ap_fixed<16, 2>;
using sample_val_t = ap_fixed<16, 2>;
using acc_val_t = ap_fixed<40, 12>;

// Complex sample: 16-bit I (real) + 16-bit Q (imaginary).
struct ComplexSample {
    sample_val_t real;
    sample_val_t imag;
};

// For SSR archictectures, Sample packet can hold multiple complex samples
struct SamplePacket {
    ComplexSample samples[NUM_SAMPLES_PER_PACKET];
};

struct FirConfig {
    coeff_t taps[NUM_TAPS];
    bool write_req;

    FirConfig() : taps{}, write_req(false) {
        taps[0] = coeff_t(1);
    }
};

void save_config(const FirConfig & axi_cfg, FirConfig & active_cfg);

// Datapath: one packet in, one packet out per clock. II = 1
class FirFilterCore {
private:
    ShiftRegister<ComplexSample, NUM_TAPS> sr_;
    ComplexSample fir_apply(const ComplexSample & in_s, const FirConfig & cfg);

public:
    FirFilterCore() = default;
    SamplePacket process(const SamplePacket & in_pkt, const FirConfig & cfg);
};

// Top-level streaming module: free-running, one packet in , one packet out per clk
void FirFilter(hls::stream<SamplePacket> & in_stream,
               hls::stream<SamplePacket> & out_stream,
               const FirConfig & axi_cfg);
