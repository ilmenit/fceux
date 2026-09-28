#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def parse_hex(text):
    return text.replace(" ", "")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("pattern", help="hex byte pattern, for example '4c', 'ad 00 20', or 'ad ?? 20'")
    parser.add_argument("--domain", default="cpu", choices=["cpu", "ppu", "rom"])
    parser.add_argument("--start")
    parser.add_argument("--end")
    parser.add_argument("--limit", type=int, default=32)
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        result = f.memsearch(
            parse_hex(args.pattern),
            domain=args.domain,
            start=args.start,
            end=args.end,
            limit=args.limit,
        )
        print(result)


if __name__ == "__main__":
    main()
