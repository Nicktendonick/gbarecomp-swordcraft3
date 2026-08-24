#!/usr/bin/env python3
"""Compare two interleaved signed-16 PCM captures without embedding audio."""

from __future__ import annotations

import argparse
import array
import json
import math
import sys
from pathlib import Path


def read_pcm(path: Path, channels: int) -> array.array:
    size = path.stat().st_size
    frame_bytes = channels * 2
    if size == 0 or size % frame_bytes:
        raise ValueError(f"{path}: size must be a nonzero multiple of {frame_bytes}")
    samples = array.array("h")
    with path.open("rb") as stream:
        samples.fromfile(stream, size // 2)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def metrics(samples: array.array, channels: int, rate: float) -> dict:
    frames = len(samples) // channels
    peak = max(abs(value) for value in samples)
    clipped = sum(abs(value) >= 32760 for value in samples)
    square = sum(value * value for value in samples)
    max_delta = 0
    previous = [0] * channels
    zero_frames = 0
    longest_zero = run = 0
    for frame in range(frames):
        values = samples[frame * channels:(frame + 1) * channels]
        if all(value == 0 for value in values):
            zero_frames += 1
            run += 1
            longest_zero = max(longest_zero, run)
        else:
            run = 0
        if frame:
            max_delta = max(max_delta, *(abs(value - previous[channel])
                                         for channel, value in enumerate(values)))
        previous[:] = values
    return {
        "frames": frames,
        "duration_seconds": frames / rate,
        "rms": math.sqrt(square / len(samples)),
        "peak": peak,
        "clipped_samples": clipped,
        "max_delta": max_delta,
        "zero_frames": zero_frames,
        "longest_zero_seconds": longest_zero / rate,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--sample-rate", type=float, required=True)
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--max-duration-delta", type=float, default=0.25)
    parser.add_argument("--min-rms-ratio", type=float, default=0.85)
    parser.add_argument("--max-rms-ratio", type=float, default=1.15)
    parser.add_argument("--max-extra-clipped", type=int, default=0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    try:
        native = metrics(read_pcm(args.native, args.channels), args.channels,
                         args.sample_rate)
        reference = metrics(read_pcm(args.reference, args.channels), args.channels,
                            args.sample_rate)
    except (OSError, ValueError) as error:
        print(f"audio comparison: ERROR: {error}", file=sys.stderr)
        return 2
    rms_ratio = native["rms"] / reference["rms"] if reference["rms"] else math.inf
    failures = []
    if abs(native["duration_seconds"] - reference["duration_seconds"]) > args.max_duration_delta:
        failures.append("duration mismatch")
    if not args.min_rms_ratio <= rms_ratio <= args.max_rms_ratio:
        failures.append("RMS ratio outside allowed range")
    if native["clipped_samples"] > reference["clipped_samples"] + args.max_extra_clipped:
        failures.append("additional clipped samples")
    report = {
        "schema": "gbarecomp-audio-comparison-v1",
        "native": native,
        "reference": reference,
        "rms_ratio": rms_ratio,
        "failures": failures,
        "passed": not failures,
    }
    rendered = json.dumps(report, indent=2)
    print(rendered)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
