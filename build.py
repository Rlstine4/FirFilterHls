#!/usr/bin/env python3
import argparse
import pathlib
import subprocess

REPO_ROOT = pathlib.Path(__file__).resolve().parent
WORK_DIR = REPO_ROOT / "fir_filter_prj"
CFG_FILE = WORK_DIR / "hls_config.cfg"

DEFAULT_FLOW = "all"
DEFAULT_NUM_TAPS = 6
DEFAULT_SAMPLES_PER_PACKET = 4
DEFAULT_PART = "xcku3p-ffva676-2-e"
DEFAULT_CLOCK = 3.33


def write_config(args: argparse.Namespace) -> None:
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    cflags = f"-I{REPO_ROOT / 'src'} -DNUM_TAPS={args.num_taps} -DNUM_SAMPLES_PER_PACKET={args.samples_per_packet}"

    CFG_FILE.write_text(
        f"part={args.part}\n\n"
        f"[hls]\n"
        f"syn.top=FirFilter\n"
        f"clock={args.clock}\n"
        f"syn.file={REPO_ROOT / 'src/FirFilter.cpp'}\n"
        f"syn.cflags={cflags}\n"
        f"tb.file={REPO_ROOT / 'tb/testbench.cpp'}\n"
        f"tb.cflags={cflags}\n"
    )


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, cwd=WORK_DIR, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and verify the FIR filter in Vitis HLS.")
    parser.add_argument("flow", nargs="?", default=DEFAULT_FLOW, choices=["csim", "csynth", "all"])
    parser.add_argument("--num-taps", type=int, default=DEFAULT_NUM_TAPS)
    parser.add_argument("--samples-per-packet", type=int, default=DEFAULT_SAMPLES_PER_PACKET)
    parser.add_argument("--part", type=str, default=DEFAULT_PART)
    parser.add_argument("--clock", type=float, default=DEFAULT_CLOCK)
    args = parser.parse_args()

    write_config(args)

    if args.flow in ("csim", "all"):
        run(["vitis-run", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", str(WORK_DIR), "--csim"])

    if args.flow in ("csynth", "all"):
        run(["v++", "--compile", "--mode", "hls", "--config", str(CFG_FILE), "--work_dir", str(WORK_DIR)])


if __name__ == "__main__":
    main()
