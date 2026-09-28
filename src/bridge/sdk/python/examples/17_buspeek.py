#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Read through the CPU bus with explicit side effects.")
    parser.add_argument("token_file")
    parser.add_argument("--addr", default="$2002")
    parser.add_argument("--len", type=int, default=1)
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        if args.len == 1:
            print(f.buspeek(args.addr))
        else:
            print(f.buspeek(args.addr, args.len))


if __name__ == "__main__":
    main()
