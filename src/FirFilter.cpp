#include "FirFilter.hpp"

ComplexSample FirFilterCore::fir_apply(const ComplexSample & in_s, const FirConfig & cfg) {
#pragma HLS INLINE

    sr_.write(in_s);

    acc_val_t acc_r = 0;
    acc_val_t acc_i = 0;

    for (std::size_t k = 0; k < _NUM_TAPS; ++k) {
#pragma HLS UNROLL
        ComplexSample delayed = sr_.read(k);
        coeff_t c = cfg.taps[k];
        acc_r += delayed.real * c;
        acc_i += delayed.imag * c;
    }

    ComplexSample out_s;
    out_s.real = static_cast<sample_val_t>(acc_r);
    out_s.imag = static_cast<sample_val_t>(acc_i);
    return out_s;
}

SamplePacket FirFilterCore::process(const SamplePacket & in_pkt, const FirConfig & cfg) {
#pragma HLS INLINE

    SamplePacket in_local = in_pkt;
    SamplePacket out_pkt;
#pragma HLS ARRAY_PARTITION variable=in_local.samples complete dim=1
#pragma HLS ARRAY_PARTITION variable=out_pkt.samples complete dim=1

    for (std::size_t s = 0; s < _NUM_SAMPLES_PER_PACKET; ++s) {
#pragma HLS UNROLL
        out_pkt.samples[s] = fir_apply(in_local.samples[s], cfg);
    }

    return out_pkt;
}

void save_config(const FirConfig & axi_cfg, FirConfig & active_cfg) {
#pragma HLS INLINE
    for (std::size_t k = 0; k < _NUM_TAPS; ++k) {
#pragma HLS UNROLL
        active_cfg.taps[k] = axi_cfg.taps[k];
    }
    active_cfg.write_req = axi_cfg.write_req;
}

// Top-level module. Free-running (ap_ctrl_none): the AXI4-Stream interfaces
// provide the flow control, so there is no ap_start/ap_done handshake.
void FirFilter(hls::stream<SamplePacket>& in_stream,
               hls::stream<SamplePacket>& out_stream,
               const FirConfig & axi_cfg) {
#pragma HLS INTERFACE axis port=in_stream
#pragma HLS INTERFACE axis port=out_stream
#pragma HLS INTERFACE s_axilite port=axi_cfg bundle=control
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS PIPELINE II=1

    static FirFilterCore filter;
    static FirConfig active_cfg;
#pragma HLS ARRAY_PARTITION variable=active_cfg.taps complete dim=1

    if (axi_cfg.write_req != active_cfg.write_req) {
        save_config(axi_cfg, active_cfg);
    }

    SamplePacket in_pkt = in_stream.read();
    SamplePacket out_pkt = filter.process(in_pkt, active_cfg);
    out_stream.write(out_pkt);
}
