#!/usr/bin/env python3
import argparse
import json
import os
import statistics
import tempfile
import time
from pathlib import Path

from fceux_bridge import FceuxBridge
from fceux_bridge.smoke import _read_token, launch_fceux, write_counter_rom


def timed_frames(f, frames):
    started = time.perf_counter()
    f.frame(frames)
    return time.perf_counter() - started


def measure(f, *, frames, iterations, history_size):
    samples = {"enabled": [], "disabled": []}
    modes = [False, True] * iterations
    for enabled in modes:
        f.history_config(size=history_size, enabled=enabled)
        f.history_clear()
        elapsed = timed_frames(f, frames)
        key = "enabled" if enabled else "disabled"
        samples[key].append(elapsed)
    return samples


def summarize(samples, frames):
    summary = {}
    for key, values in samples.items():
        mean = statistics.fmean(values) if values else 0.0
        summary[key] = {
            "samples": values,
            "mean_seconds": mean,
            "mean_ms_per_frame": (mean / frames) * 1000.0 if frames else 0.0,
            "min_seconds": min(values) if values else 0.0,
            "max_seconds": max(values) if values else 0.0,
        }
    disabled = summary["disabled"]["mean_seconds"]
    enabled = summary["enabled"]["mean_seconds"]
    delta = enabled - disabled
    summary["overhead"] = {
        "delta_seconds": delta,
        "delta_ms_per_frame": (delta / frames) * 1000.0 if frames else 0.0,
        "ratio": (enabled / disabled) if disabled > 0 else None,
    }
    return summary


def connect_existing(token_file, timeout):
    return FceuxBridge.from_token_file(token_file, timeout=timeout), None


def launch_bridge(args, tempdir):
    rom = Path(args.rom) if args.rom else write_counter_rom(Path(tempdir) / "history overhead.nes")
    env = {}
    if args.qt_offscreen or (args.bridge_headless and "QT_QPA_PLATFORM" not in os.environ):
        env["QT_QPA_PLATFORM"] = "offscreen"
    if args.sdl_dummy:
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
    process, token_file = launch_fceux(
        args.fceux,
        rom,
        bridge=args.bridge,
        timeout=args.timeout,
        env=env,
        bridge_headless=args.bridge_headless,
        bridge_arg_style=args.bridge_arg_style,
    )
    endpoint, token = _read_token(token_file)
    if endpoint == "stdio":
        bridge = FceuxBridge.from_process_stdio(process, token, timeout=args.timeout)
    else:
        bridge = FceuxBridge.from_token_file(token_file, timeout=args.timeout)
    return bridge, process


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except Exception:
        process.kill()


def main():
    parser = argparse.ArgumentParser(description="Measure bridge execution-history capture overhead.")
    parser.add_argument("--token-file", help="attach to an existing bridge token file")
    parser.add_argument("--fceux", help="FCEUX executable to launch when --token-file is omitted")
    parser.add_argument("--rom", help="ROM to launch; defaults to a generated NROM test ROM")
    parser.add_argument("--bridge", default="stdio", help="bridge endpoint for launched FCEUX")
    parser.add_argument("--bridge-headless", action="store_true", help="launch FCEUX in bridge-headless mode")
    parser.add_argument("--bridge-arg-style", choices=("equals", "split"), default="split")
    parser.add_argument("--qt-offscreen", action="store_true", help="set QT_QPA_PLATFORM=offscreen")
    parser.add_argument("--sdl-dummy", action="store_true", help="set SDL dummy video/audio drivers")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--history-size", type=int, default=16384)
    args = parser.parse_args()

    if args.frames <= 0:
        parser.error("--frames must be positive")
    if args.iterations <= 0:
        parser.error("--iterations must be positive")
    if args.history_size <= 0:
        parser.error("--history-size must be positive")
    if not args.token_file and not args.fceux:
        parser.error("either --token-file or --fceux is required")

    process = None
    with tempfile.TemporaryDirectory(prefix="fceux-bridge-history-") as tempdir:
        if args.token_file:
            bridge, process = connect_existing(args.token_file, args.timeout)
        else:
            bridge, process = launch_bridge(args, tempdir)
        try:
            with bridge as f:
                status = f.status()
                f.frame(5)
                samples = measure(
                    f,
                    frames=args.frames,
                    iterations=args.iterations,
                    history_size=args.history_size,
                )
                result = {
                    "ok": True,
                    "frames_per_sample": args.frames,
                    "iterations_per_mode": args.iterations,
                    "history_size": args.history_size,
                    "status": {
                        "game_loaded": status.get("game_loaded"),
                        "rom": status.get("rom"),
                    },
                    "summary": summarize(samples, args.frames),
                }
                print(json.dumps(result, indent=2, sort_keys=True))
                if process is not None:
                    try:
                        f.app_exit()
                    except Exception:
                        pass
        finally:
            stop_process(process)


if __name__ == "__main__":
    main()
