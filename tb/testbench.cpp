#include <cmath>
#include <iostream>
#include <vector>
#include "FirFilter.hpp"

// Software model of the datapath: keeps history, consumes one beat at a time.
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

    SamplePacket step(const SamplePacket& in_pkt, const ActiveConfig& cfg) {
        SamplePacket out_pkt;
        for (int s = 0; s < SSR; ++s) {
            for (int k = NUM_TAPS - 1; k > 0; --k) {
                history_[k] = history_[k - 1];
            }
            history_[0] = in_pkt.samples[s];

            acc_val_t acc_r = 0;
            acc_val_t acc_i = 0;
            for (int k = 0; k < NUM_TAPS; ++k) {
                acc_val_t c = (acc_val_t)cfg.taps[k];
                acc_r += (acc_val_t)history_[k].real * c;
                acc_i += (acc_val_t)history_[k].imag * c;
            }
            out_pkt.samples[s].real = (sample_val_t)acc_r;
            out_pkt.samples[s].imag = (sample_val_t)acc_i;
        }
        return out_pkt;
    }
};

static ActiveConfig committed(const FirConfig& axi_cfg) {
    ActiveConfig active;
    for (int k = 0; k < NUM_TAPS; ++k) {
        active.taps[k] = axi_cfg.taps[k];
    }
    active.write_config = axi_cfg.write_config;
    return active;
}

static FirConfig make_config(double scale, bool write_config) {
    FirConfig cfg;
    cfg.write_config = write_config;
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
    for (int s = 0; s < SSR; ++s) {
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
                     const ActiveConfig& expected,
                     GoldenFirModel& golden,
                     int& sample_index) {
    hls::stream<SamplePacket> in_stream;
    hls::stream<SamplePacket> out_stream;

    const int beats = NUM_BEATS;
    std::vector<SamplePacket> in_beats(beats);
    for (int b = 0; b < beats; ++b) {
        in_beats[b] = make_input(sample_index + b * SSR);
        in_stream.write(in_beats[b]);
    }
    sample_index += beats * SSR;

    FirFilter(in_stream, out_stream, axi_cfg);

    int mismatches = 0;
    for (int b = 0; b < beats; ++b) {
        SamplePacket hw = out_stream.read();
        SamplePacket gold = golden.step(in_beats[b], expected);
        for (int s = 0; s < SSR; ++s) {
            if (hw.samples[s].real != gold.samples[s].real ||
                hw.samples[s].imag != gold.samples[s].imag) {
                if (mismatches < 5) {
                    std::cerr << name << ": mismatch beat " << b << " lane " << s
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
              << ", SSR=" << SSR << ", bus_bits=" << (32 * SSR)
              << ", beats=" << beats << "): "
              << (mismatches == 0 ? "PASS" : "FAIL") << "\n";
    return mismatches;
}

int main() {
    GoldenFirModel golden;
    int sample_index = 0;
    int total_mismatches = 0;

    // write_config false: first beat always adopts the register contents.
    FirConfig cfg_v1 = make_config(1.00, false);
    ActiveConfig active_v1 = committed(cfg_v1);

    // Same tap values, still no toggle: hardware must keep using v1.
    FirConfig cfg_partial = cfg_v1;
    for (int k = 0; k < NUM_TAPS / 2; ++k) {
        cfg_partial.taps[k] = coeff_t(0.125);
    }

    // Toggle write_config: the whole new tap set is committed in one cycle.
    FirConfig cfg_v2 = make_config(0.65, true);
    ActiveConfig active_v2 = committed(cfg_v2);

    total_mismatches += run_phase("Phase 1  adopt cfg v1 (no toggle)",
                                  cfg_v1, active_v1, golden, sample_index);

    total_mismatches += run_phase("Phase 2  taps rewritten, no toggle",
                                  cfg_partial, active_v1, golden, sample_index);

    total_mismatches += run_phase("Phase 3  write_config toggled -> cfg v2",
                                  cfg_v2, active_v2, golden, sample_index);

    if (total_mismatches == 0) {
        std::cout << "OVERALL: PASS\n";
        return 0;
    }
    std::cout << "OVERALL: FAIL\n";
    return 1;
}
