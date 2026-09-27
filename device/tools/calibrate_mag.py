#!/usr/bin/env python3
"""
Magnetometer calibration helper for the imu-sketch firmware.

Reads MotionCal-format `Raw:` lines from the device's serial port while the
user rotates the device through all orientations (slow figure-8s in 3D), then
fits a simple bounding-box ellipsoid to the magnetometer point cloud and
prints hard-iron offsets and per-axis soft-iron scale factors.

Usage:
    python3 calibrate_mag.py /dev/cu.usbmodem2101 [--seconds 90]

Requires:
    pip install pyserial numpy

Output:
    Hard-iron offset (uT)    -> apply as: mx -= offset_x; my -= offset_y; mz -= offset_z
    Soft-iron scale          -> apply as: mx *= scale_x; my *= scale_y; mz *= scale_z

The expected procedure: start the script, then slowly rotate the device through
all orientations for the duration. You want the point cloud to fill a sphere,
not a band. Slow figure-8s with the device turned in your hand work well.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("ERROR: pyserial not installed. Run: pip install pyserial")

try:
    import numpy as np
except ImportError:
    sys.exit("ERROR: numpy not installed. Run: pip install numpy")


def collect_samples(port: str, seconds: float) -> np.ndarray:
    """Read Raw: lines from `port` for `seconds`, return mag samples in uT."""
    print(f"Opening {port} at 115200 baud...")
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        # Drain any stale buffered data
        ser.reset_input_buffer()

        print(f"Collecting for {seconds:.0f}s — rotate the device slowly in all orientations.")
        print("Press Ctrl-C to stop early.\n")

        samples: list[tuple[float, float, float]] = []
        deadline = time.monotonic() + seconds
        last_status = 0.0

        try:
            while time.monotonic() < deadline:
                line = ser.readline().decode("ascii", errors="ignore").strip()
                if not line.startswith("Raw:"):
                    continue
                try:
                    values = [int(v) for v in line[4:].split(",")]
                except ValueError:
                    continue
                if len(values) != 9:
                    continue
                # Last three are mag, scaled at 10 LSB/uT in the sketch
                mx, my, mz = values[6] / 10.0, values[7] / 10.0, values[8] / 10.0
                samples.append((mx, my, mz))

                now = time.monotonic()
                if now - last_status >= 1.0:
                    last_status = now
                    remaining = deadline - now
                    print(
                        f"\rsamples={len(samples):>5}  "
                        f"latest=({mx:+6.1f}, {my:+6.1f}, {mz:+6.1f}) uT  "
                        f"{remaining:4.0f}s left",
                        end="",
                        flush=True,
                    )
        except KeyboardInterrupt:
            print("\nStopped early by user.")

    print()
    return np.array(samples, dtype=float)


def fit(data: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    """Bounding-box ellipsoid fit. Returns (offset, scale, residual_std)."""
    if len(data) < 200:
        sys.exit(f"ERROR: only {len(data)} samples — need at least ~200 for a usable fit.")

    mins = data.min(axis=0)
    maxs = data.max(axis=0)
    offset = (maxs + mins) / 2

    radii = (maxs - mins) / 2
    avg_radius = radii.mean()
    if (radii < avg_radius * 0.3).any():
        print("\nWARNING: one axis has a much smaller range than the others.")
        print("         You probably didn't rotate enough on that axis.")
        print(f"         Per-axis radii (uT): {radii}")

    scale = avg_radius / radii

    # Residual: corrected points should lie on a unit sphere if calibration is perfect
    corrected = (data - offset) * scale / avg_radius
    norms = np.linalg.norm(corrected, axis=1)
    residual_std = float(norms.std())

    return offset, scale, residual_std


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="Serial port, e.g. /dev/cu.usbmodem2101")
    ap.add_argument("--seconds", type=float, default=90.0, help="Capture duration (default 90)")
    args = ap.parse_args()

    data = collect_samples(args.port, args.seconds)
    if len(data) == 0:
        sys.exit("ERROR: no Raw: lines received. Is the sketch in RAW mode?")

    offset, scale, residual_std = fit(data)

    mins, maxs = data.min(axis=0), data.max(axis=0)
    print(f"\nCollected {len(data)} samples")
    print(f"Range (uT):  x [{mins[0]:+7.1f} .. {maxs[0]:+7.1f}]   "
          f"y [{mins[1]:+7.1f} .. {maxs[1]:+7.1f}]   "
          f"z [{mins[2]:+7.1f} .. {maxs[2]:+7.1f}]")
    print(f"Residual stddev (lower is better, < 0.05 is good): {residual_std:.3f}")

    print("\n--- Calibration constants ---")
    print(f"  hard_iron_offset_uT = {{ {offset[0]:+.3f}f, {offset[1]:+.3f}f, {offset[2]:+.3f}f }}")
    print(f"  soft_iron_scale     = {{ {scale[0]:.4f}f, {scale[1]:.4f}f, {scale[2]:.4f}f }}")
    print()
    print("Apply in your IMU task before filter.update():")
    print("    mx = (mx_raw - hard_iron_offset_uT[0]) * soft_iron_scale[0];")
    print("    my = (my_raw - hard_iron_offset_uT[1]) * soft_iron_scale[1];")
    print("    mz = (mz_raw - hard_iron_offset_uT[2]) * soft_iron_scale[2];")


if __name__ == "__main__":
    main()
