#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("--count", type=int, default=16)
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.trace_status())
        for entry in f.history(args.count, include_disasm=True)["entries"]:
            opcode = " ".join(f"{byte:02x}" for byte in entry["opcode"])
            print(
                f'{entry["seq"]:>8} {entry["PC"]} '
                f'{opcode:<8} {entry["disasm"]}'
            )


if __name__ == "__main__":
    main()
