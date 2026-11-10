#!/usr/bin/env python3
"""
Modern Vitis HLS (2024.x / 2025.x / 2026.x) Unified Build & Flow Script.

In modern Vitis HLS, the legacy Tcl flow (`vitis_hls -f script.tcl`) is replaced
by a declarative `hls_config.cfg` file driven by the unified `v++` and `vitis-run` CLIs:
  - C Simulation (csim)       : vitis-run --mode hls --config hls_config.cfg --work_dir fir_filter --csim
  - C Synthesis (csynth)      : v++ --compile --mode hls --config hls_config.cfg --work_dir fir_filter
  - C/RTL Co-Simulation(cosim): vitis-run --mode hls --config hls_config.cfg --work_dir fir_filter --cosim
  - Implementation (impl)     : vitis-run --mode hls --config hls_config.cfg --work_dir fir_filter --impl

This script:
  1. Generates `hls_config.cfg` (and `vitis-comp.json`) with your chosen compile-time
     parameters (`--num-taps`, `--ssr`, `--beats`, `--part`, `--clock`).
  2. Executes the requested flow (`configure`, `host_sim`, `csim`, `csynth`, `cosim`, `impl`, or `all`).
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parent
CFG_FILE = REPO_ROOT / "hls_config.cfg"
COMP_FILE = REPO_ROOT / "vitis-comp.json"
TOP_FUNCTION = "FirFilter"
WORK_DIR = TOP_FUNCTION


def write_hls_config(num_taps: int, ssr: int, beats: int, part: str, clock: float, trace: bool) -> None:
    cflags = f"-Isrc -DNUM_TAPS={num_taps} -DSSR={ssr} -DNUM_BEATS={beats}"
    trace_level = "all" if trace else "none"
    cfg_text = f"""# Note: part= must be at the top level outside [hls]
# Note: clock= uses ns period
part={part}

[hls]
syn.top={TOP_FUNCTION}
clock={clock}
syn.file=src/FirFilter.cpp
syn.cflags={cflags}
tb.file=tb/testbench.cpp
tb.cflags={cflags}
cosim.trace_level={trace_level}
"""
    CFG_FILE.write_text(cfg_text)

    comp_text = f"""{{
   "name": "{TOP_FUNCTION}",
   "type": "HLS",
   "configuration": {{
    "componentType": "HLS",
    "configFiles": ["./hls_config.cfg"],
    "work_dir": "{WORK_DIR}"
   }}
}}
"""
    COMP_FILE.write_text(comp_text)
    print(
        f"[build.py] Generated {CFG_FILE.name}: "
        f"NUM_TAPS={num_taps}, SSR={ssr}, NUM_BEATS={beats} (testbench only), part={part}, clock={clock}ns"
    )


def run_cmd(cmd: list[str]) -> None:
    print(f"[build.py] Running: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


def run_host_sim(num_taps: int, ssr: int, beats: int) -> None:
    """Fast g++ functional verification (uses $XILINX_VITIS/include or .hls_compat/include fallback)."""
    vitis_root = os.environ.get("XILINX_VITIS")
    if vitis_root and pathlib.Path(vitis_root, "include", "ap_fixed.h").exists():
        hls_inc = str(pathlib.Path(vitis_root, "include"))
    else:
        hls_inc = str(REPO_ROOT / ".hls_compat" / "include")

    out_bin = REPO_ROOT / "verify_fir"
    cmd = [
        "g++",
        "-std=c++14",
        "-O2",
        "-Wno-unknown-pragmas",
        "-Isrc",
        f"-I{hls_inc}",
        f"-DNUM_TAPS={num_taps}",
        f"-DSSR={ssr}",
        f"-DNUM_BEATS={beats}",
        "src/FirFilter.cpp",
        "tb/testbench.cpp",
        "-o",
        str(out_bin),
    ]
    run_cmd(cmd)
    try:
        run_cmd([str(out_bin)])
    finally:
        if out_bin.exists():
            out_bin.unlink()


def require_tool(tool_name: str) -> None:
    if shutil.which(tool_name) is None:
        print(
            f"Error: '{tool_name}' not found in PATH. "
            "Please source your Vitis HLS 2026 settings64.sh once installation completes.",
            file=sys.stderr,
        )
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Configure and run Vitis HLS flows for the complex-sample, real-tap, SSR FIR filter."
    )
    parser.add_argument(
        "flow",
        nargs="?",
        default="host_sim",
        choices=["configure", "host_sim", "csim", "csynth", "cosim", "impl", "all"],
        help="Flow stage to execute (default: host_sim)",
    )
    parser.add_argument("--num-taps", type=int, default=8, help="Number of real FIR taps (default: 8)")
    parser.add_argument("--ssr", type=int, default=4, help="Super Sample Rate: 1, 2, 4, 8, ... (default: 4)")
    parser.add_argument(
        "--beats",
        type=int,
        default=128,
        help="AXI4-Stream beats per phase in the testbench (default: 128); not a kernel parameter",
    )
    parser.add_argument("--part", type=str, default="xczu9eg-ffvb1156-2-e", help="Target FPGA part")
    parser.add_argument("--clock", type=float, default=3.33, help="Target clock period in ns (default: 3.33)")
    parser.add_argument("--trace", action="store_true", help="Enable full waveform dumping in cosim")
    args = parser.parse_args()

    if args.num_taps < 2 or args.num_taps > 1024:
        print("Error: --num-taps must be between 2 and 1024", file=sys.stderr)
        sys.exit(1)

    if args.ssr < 1 or (args.ssr & (args.ssr - 1)) != 0:
        print("Error: --ssr must be a power of 2 (1, 2, 4, 8, ...)", file=sys.stderr)
        sys.exit(1)

    write_hls_config(args.num_taps, args.ssr, args.beats, args.part, args.clock, args.trace)

    if args.flow == "configure":
        return

    if args.flow == "host_sim":
        run_host_sim(args.num_taps, args.ssr, args.beats)
        return

    if args.flow in ("csim", "all"):
        require_tool("vitis-run")
        run_cmd(["vitis-run", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", WORK_DIR, "--csim"])

    if args.flow in ("csynth", "all"):
        require_tool("v++")
        run_cmd(["v++", "--compile", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", WORK_DIR])

    if args.flow in ("cosim", "all"):
        require_tool("vitis-run")
        syn_dir = REPO_ROOT / WORK_DIR / "hls" / "syn"
        if not syn_dir.exists():
            require_tool("v++")
            run_cmd(["v++", "--compile", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", WORK_DIR])
        run_cmd(["vitis-run", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", WORK_DIR, "--cosim"])

    if args.flow in ("impl", "all"):
        require_tool("vitis-run")
        run_cmd(["vitis-run", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", WORK_DIR, "--impl"])


if __name__ == "__main__":
    main()
