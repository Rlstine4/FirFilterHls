#include <cmath>
#include <iostream>
#include <vector>
#include "FirFilter.hpp"

// Testbench-only: number of clocks per phase.
constexpr int NUM_CLOCKS = 128;

// Software model of the datapath: keeps history, consumes one packet per clock.
class GoldenFirModel {
private:
    ComplexSample history_[NUM_TAPS];

public:
    GoldenFirModel() {
        for (int k = 0; k < NUM_TAPS; ++k) {
            history_[k].real = sample_val_t(0);
            history_[k].imag = sample_val_t(0);
        }
    }

    SamplePacket step(const SamplePacket& in_pkt, const FirConfig& cfg) {
        SamplePacket out_pkt;
        for (int s = 0; s < NUM_SAMPLES_PER_PACKET; ++s) {
            for (int k = NUM_TAPS - 1; k > 0; --k) {
                history_[k] = history_[k - 1];
            }
            history_[0] = in_pkt.samples[s];

            acc_val_t acc_r = 0;
            acc_val_t acc_i = 0;
            for (int k = 0; k < NUM_TAPS; ++k) {
                coeff_t c = cfg.taps[k];
                acc_r += history_[k].real * c;
                acc_i += history_[k].imag * c;
            }
            out_pkt.samples[s].real = static_cast<sample_val_t>(acc_r);
            out_pkt.samples[s].imag = static_cast<sample_val_t>(acc_i);
        }
        return out_pkt;
    }
};

static FirConfig make_config(double scale, bool write_req) {
    FirConfig cfg;
    cfg.write_req = write_req;
    for (int k = 0; k < NUM_TAPS; ++k) {
        double center = 0.5 * (NUM_TAPS - 1);
        double rel = (k - center) / (0.5 * NUM_TAPS);
        double window = 0.54 - 0.46 * std::cos(2.0 * 3.141592653589793 * k / (NUM_TAPS - 1));
        double c = scale * window * (1.0 - 0.5 * rel * rel) / (NUM_TAPS * 0.45);
        cfg.taps[k] = coeff_t(c);
    }
    return cfg;
}

static SamplePacket make_input(int base_sample) {
    SamplePacket pkt;
    for (int s = 0; s < NUM_SAMPLES_PER_PACKET; ++s) {
        int n = base_sample + s;
        double re = 0.50 * std::cos(2.0 * 3.141592653589793 * n / 16.0)
                  + 0.20 * std::sin(2.0 * 3.141592653589793 * n / 5.0);
        double im = 0.50 * std::sin(2.0 * 3.141592653589793 * n / 16.0)
                  - 0.20 * std::cos(2.0 * 3.141592653589793 * n / 7.0);
        pkt.samples[s].real = sample_val_t(re);
        pkt.samples[s].imag = sample_val_t(im);
    }
    return pkt;
}

static int run_phase(const char* name,
                     const FirConfig& axi_cfg,
                     const FirConfig& expected,
                     GoldenFirModel& golden,
                     int& sample_index) {
    hls::stream<SamplePacket> in_stream;
    hls::stream<SamplePacket> out_stream;

    const int clocks = NUM_CLOCKS;
    std::vector<SamplePacket> in_packets(clocks);
    for (int c = 0; c < clocks; ++c) {
        in_packets[c] = make_input(sample_index + c * NUM_SAMPLES_PER_PACKET);
        in_stream.write(in_packets[c]);
        FirFilter(in_stream, out_stream, axi_cfg);
    }
    sample_index += clocks * NUM_SAMPLES_PER_PACKET;

    int mismatches = 0;
    for (int c = 0; c < clocks; ++c) {
        SamplePacket hw = out_stream.read();
        SamplePacket gold = golden.step(in_packets[c], expected);
        for (int s = 0; s < NUM_SAMPLES_PER_PACKET; ++s) {
            if (hw.samples[s].real != gold.samples[s].real ||
                hw.samples[s].imag != gold.samples[s].imag) {
                if (mismatches < 5) {
                    std::cerr << name << ": mismatch clock " << c << " sample " << s
                              << " got (" << hw.samples[s].real.to_double()
                              << ", " << hw.samples[s].imag.to_double()
                              << ") expected (" << gold.samples[s].real.to_double()
                              << ", " << gold.samples[s].imag.to_double() << ")\n";
                }
                ++mismatches;
            }
        }
    }

    std::cout << name << " (NUM_TAPS=" << NUM_TAPS
              << ", NUM_SAMPLES_PER_PACKET=" << NUM_SAMPLES_PER_PACKET
              << ", bus_bits=" << (32 * NUM_SAMPLES_PER_PACKET)
              << ", clocks=" << clocks << "): "
              << (mismatches == 0 ? "PASS" : "FAIL") << "\n";
    return mismatches;
}

int main() {
    GoldenFirModel golden;
    int sample_index = 0;
    int total_mismatches = 0;

    // Default passthrough config on startup: taps = {1, 0, ..., 0}, write_req = false.
    FirConfig cfg_passthrough;

    // Taps written to AXI4-Lite without toggling write_req: hardware stays on passthrough.
    FirConfig cfg_uncommitted = make_config(1.00, false);

    // Toggle write_req (false -> true): commits v1 tap set in one clock.
    FirConfig cfg_v1 = make_config(1.00, true);

    // Taps rewritten without toggling write_req: hardware keeps using v1.
    FirConfig cfg_partial = cfg_v1;
    for (int k = 0; k < NUM_TAPS / 2; ++k) {
        cfg_partial.taps[k] = coeff_t(0.125);
    }

    // Toggle write_req again (true -> false): commits v2 tap set in one clock.
    FirConfig cfg_v2 = make_config(0.65, false);

    total_mismatches += run_phase("Phase 1  startup passthrough (tapsrewritten, no toggle)",
                                  cfg_uncommitted, cfg_passthrough, golden, sample_index);

    total_mismatches += run_phase("Phase 2  write_req toggled -> cfg v1",
                                  cfg_v1, cfg_v1, golden, sample_index);

    total_mismatches += run_phase("Phase 3  taps rewritten, no toggle",
                                  cfg_partial, cfg_v1, golden, sample_index);

    total_mismatches += run_phase("Phase 4  write_req toggled -> cfg v2",
                                  cfg_v2, cfg_v2, golden, sample_index);

    if (total_mismatches == 0) {
        std::cout << "OVERALL: PASS\n";
        return 0;
    }
    std::cout << "OVERALL: FAIL\n";
    return 1;
}
