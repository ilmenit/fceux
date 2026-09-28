#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Dump direct PPU memory without invoking PPU read hooks.")
    parser.add_argument("token_file")
    parser.add_argument("--domain", default="nametable", choices=("pattern", "nametable", "palette", "all"))
    parser.add_argument("--output", default="/tmp/fceux-bridge-ppu.bin")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        dump = f.ppu_dump(domain=args.domain)
        with open(args.output, "wb") as out:
            out.write(dump["bytes"])
        print({"domain": dump["domain"], "addr": dump["addr"], "len": dump["len"], "output": args.output})


if __name__ == "__main__":
    main()
