#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("--addr", default="$8000")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.cart_info())
        print(f.bank_info(args.addr))
        print(f.memmap())
        print(f.rom_peek(0, 16, domain="prg"))


if __name__ == "__main__":
    main()
