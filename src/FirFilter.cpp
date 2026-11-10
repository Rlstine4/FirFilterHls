#include "FirFilter.hpp"

void save_config(const FirConfig& axi_cfg, ActiveConfig& active_cfg) {
#pragma HLS INLINE
    for (int k = 0; k < NUM_TAPS; ++k) {
#pragma HLS UNROLL
        active_cfg.taps[k] = axi_cfg.taps[k];
    }
    active_cfg.write_config = axi_cfg.write_config;
}

bool config_toggled(const FirConfig& axi_cfg, const ActiveConfig& active_cfg) {
#pragma HLS INLINE
    return axi_cfg.write_config != active_cfg.write_config;
}

FirFilterCore::FirFilterCore() {}

SamplePacket FirFilterCore::process(const SamplePacket& in_pkt, const ActiveConfig& cfg) {
#pragma HLS INLINE

    SamplePacket in_local = in_pkt;
    SamplePacket out_pkt;
#pragma HLS ARRAY_PARTITION variable=in_local.samples complete dim=1
#pragma HLS ARRAY_PARTITION variable=out_pkt.samples complete dim=1

    for (int s = 0; s < SSR; ++s) {
#pragma HLS UNROLL
        ComplexSample in_s = in_local.samples[s];
        cb_.write(in_s);

        acc_val_t acc_r = 0;
        acc_val_t acc_i = 0;

        for (int k = 0; k < NUM_TAPS; ++k) {
#pragma HLS UNROLL
            ComplexSample delayed = cb_.read(k);
            acc_val_t c = (acc_val_t)cfg.taps[k];
            acc_r += (acc_val_t)delayed.real * c;
            acc_i += (acc_val_t)delayed.imag * c;
        }

        ComplexSample out_s;
        out_s.real = (sample_val_t)acc_r;
        out_s.imag = (sample_val_t)acc_i;
        out_pkt.samples[s] = out_s;
    }

    return out_pkt;
}

// Top-level module. Free-running (ap_ctrl_none): the AXI4-Stream interfaces
// provide the flow control, so there is no ap_start/ap_done handshake.
void FirFilter(hls::stream<SamplePacket>& in_stream,
               hls::stream<SamplePacket>& out_stream,
               const FirConfig& axi_cfg) {
#pragma HLS INTERFACE axis port=in_stream
#pragma HLS INTERFACE axis port=out_stream
#pragma HLS INTERFACE s_axilite port=axi_cfg bundle=control
#pragma HLS INTERFACE ap_ctrl_none port=return

    // Persistent across beats: filter history and the committed tap set.
    static FirFilterCore filter;
    static ActiveConfig active_cfg;
    static bool cfg_ready = false;
#pragma HLS ARRAY_PARTITION variable=active_cfg.taps complete dim=1

#ifdef __SYNTHESIS__
    while (true) {
#else
    for (int n = 0; n < NUM_BEATS; ++n) {
#endif
#pragma HLS PIPELINE II=1
        // Adopt the AXI4-Lite tap set on the first beat and on every toggle of
        // write_config. Between toggles the committed set cannot change, so a
        // driver that is still writing taps cannot tear live samples.
        if (!cfg_ready || config_toggled(axi_cfg, active_cfg)) {
            save_config(axi_cfg, active_cfg);
            cfg_ready = true;
        }

        SamplePacket in_pkt = in_stream.read();
        SamplePacket out_pkt = filter.process(in_pkt, active_cfg);
        out_stream.write(out_pkt);
    }
}
