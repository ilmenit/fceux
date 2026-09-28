#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("--symbols", help="optional ld65 .dbg or FCEUX .nl file")
    parser.add_argument("--bank", type=lambda value: int(value, 0), help="bank/page for explicit .nl files")
    parser.add_argument("--name", default="main")
    parser.add_argument("--addr", default="$8000")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        if args.symbols:
            print(f.sym_load(args.symbols, bank=args.bank))
        else:
            print(f.sym_load(auto=True))

        print(f.sym_resolve(args.name))
        print(f.sym_lookup(args.addr))


if __name__ == "__main__":
    main()
