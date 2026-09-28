#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("--frames", type=int, default=1)
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.ppu_state())
        print(f.apu_state())
        print(f.cdlog_start())
        f.frame(args.frames)
        dump = f.cdlog_dump(domain="all")
        print(dump["status"])
        print({"cpu_log_bytes": dump["cpu"]["size"] if dump["cpu"] else 0})
        print({"ppu_log_bytes": dump["ppu"]["size"] if dump["ppu"] else 0})
        print(f.cdlog_stop())


if __name__ == "__main__":
    main()
