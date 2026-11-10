# Accelerated FIR Filter

High-throughput, free-running streaming complex FIR filter implemented in Vitis HLS.

## Features

- **Complex I/Q datapath**
- **Real programmable coefficients**: `ap_fixed<16, 2>` taps (`coeff_t`) runtime-configurable over AXI4-Lite.
- **Super-sample-rate (SSR) streaming**: Processes `NUM_SAMPLES_PER_PACKET` complex samples per clock (`II = 1`) over an AXI4-Stream interface of width `32 * NUM_SAMPLES_PER_PACKET` bits.
- **Tear-free coefficient updates**: In-flight packets always see a coherent tap set; new coefficients are latched atomically in a single clock cycle when `write_req` is toggled.
- **Free-running kernel**: `ap_ctrl_none` control interface.

## Project Structure

```text
.
|-- src/
|   |-- ShiftRegister.hpp    # Shift-register sample history buffer
|   |-- FirFilter.hpp        # Data types, FirConfig, and FirFilterCore declarations
|   `-- FirFilter.cpp        # Datapath implementation and top-level FirFilter() kernel
|-- tb/
|   `-- testbench.cpp        # Golden reference model and verification testbench
`-- build.py                 # Vitis HLS build script (outputs to fir_filter_prj/)
```

## Architecture Overview

| Component | File | Description |
| --- | --- | --- |
| `ShiftRegister<T, N>` | `src/ShiftRegister.hpp` | Partitioned shift-register history buffer (`write()` shifts and inserts at `0`, `read(desired_idx)` returns `buf_[desired_idx]`) |
| `FirConfig` | `src/FirFilter.hpp` | Coefficient array `taps[NUM_TAPS]` and `write_req` toggle flag, shared by the AXI4-Lite interface and active register bank |
| `save_config()` | `src/FirFilter.cpp` | Unrolled single-cycle copy from AXI4-Lite registers to the active coefficient bank |
| `FirFilterCore::process()` | `src/FirFilter.cpp` | Computes `NUM_SAMPLES_PER_PACKET` complex FIR outputs in parallel for one clock |
| `FirFilter()` | `src/FirFilter.cpp` | Top-level `II=1` free-running streaming kernel |

## Hardware Interfaces

| Port | Protocol | Description |
| --- | --- | --- |
| `in_stream` | AXI4-Stream (`axis`) | Input stream of `SamplePacket` (`32 * NUM_SAMPLES_PER_PACKET` bits) |
| `out_stream` | AXI4-Stream (`axis`) | Output stream of `SamplePacket` (`32 * NUM_SAMPLES_PER_PACKET` bits) |
| `axi_cfg` | AXI4-Lite (`s_axilite`) | Register bank for `FirConfig` (`control` bundle) |
| `return` | `ap_ctrl_none` | Free-running kernel with no `ap_start` / `ap_done` handshake |

## Coefficient Update Protocol

To update filter taps at runtime:

1. Write `taps[0 .. NUM_TAPS-1]` in the AXI4-Lite `control` register space.
2. Toggle the `write_req` bit (`0 -> 1` or `1 -> 0`).

The kernel compares `axi_cfg.write_req` against its latched `active_cfg.write_req` on every clock. When a toggle is detected, `save_config()` copies all `NUM_TAPS` coefficients into `active_cfg` in a single clock cycle.

## Build and Verification

All build artifacts, configs, logs, and reports are placed in `fir_filter_prj/`.

```bash
# Run full flow (csim + csynth)
python3 build.py

# Vitis HLS C simulation only
python3 build.py csim

# Vitis HLS C synthesis only
python3 build.py csynth
```

### Build Options

| Flag | Default | Description |
| --- | --- | --- |
| `--num-taps` | `6` | Number of real FIR taps (`NUM_TAPS`) |
| `--samples-per-packet` | `4` | Complex samples per packet / clock (`NUM_SAMPLES_PER_PACKET`) |
| `--part` | `xcku3p-ffva676-2-e` | Target FPGA part |
| `--clock` | `3.33` | Target clock period in nanoseconds (`3.33` ns = 300 MHz) |

> **TODO**: Fix C/RTL Co-Simulation (`cosim`) support for the `ap_ctrl_none` + `s_axilite` top-level interface. May need to add ap_ctrl_hs just for COSIM

## Sample Linux Userspace Driver

The AXI4-Lite `s_axi_control` interface exposes `FirConfig` starting at byte offset `0x10` (packed 16-bit taps followed by `write_req`). Because hardware interprets each 16-bit tap register directly as the raw `ap_fixed<16, 2>` (`Q2.14`) bit pattern, pre-scale normalized floating-point coefficients by $2^{14} = 16384$ when generating the `int16_t` tap values in software (`16384` corresponds to `1.0`).

Below is a sample driver that may be used to control the module from userspace at runtime:

```cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

template <size_t NumTaps = 6>
class FirFilterDriver {
public:
    explicit FirFilterDriver(uintptr_t phys_base_addr) {
        fd_ = ::open("/dev/mem", O_RDWR | O_SYNC);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open /dev/mem");
        }

        map_base_ = ::mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, phys_base_addr);
        if (map_base_ == MAP_FAILED) {
            ::close(fd_);
            throw std::runtime_error("mmap failed");
        }

        regs_ = reinterpret_cast<volatile HardwareConfig*>(static_cast<uint8_t*>(map_base_) + AXI_CFG_OFFSET);
        write_req_state_ = regs_->write_req;
    }

    ~FirFilterDriver() {
        if (map_base_ != nullptr && map_base_ != MAP_FAILED) {
            ::munmap(map_base_, MAP_SIZE);
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    FirFilterDriver(const FirFilterDriver &) = delete;
    FirFilterDriver& operator=(const FirFilterDriver&) = delete;

    void set_taps(const std::array<int16_t, NumTaps> & taps) {
        for (size_t k = 0; k < NumTaps; ++k) {
            regs_->taps[k] = taps[k];
        }
    }

    void commit() {
        write_req_state_ = !write_req_state_;
        regs_->write_req = write_req_state_;
    }

private:
    static constexpr size_t MAP_SIZE       = 4096;
    static constexpr size_t AXI_CFG_OFFSET = 0x10;

    struct HardwareConfig {
        int16_t taps[NumTaps];
        bool write_req;
    };

    int fd_{-1};
    void* map_base_{nullptr};
    volatile HardwareConfig* regs_{nullptr};
    bool write_req_state_{false};
};

int main() {
    constexpr uintptr_t FIR_BASE_ADDR = 0xA0000000;
    FirFilterDriver<6> fir(FIR_BASE_ADDR);

    fir.set_taps({1, 0, 0, 0, 0, 0});
    fir.commit();
    return 0;
}
```
