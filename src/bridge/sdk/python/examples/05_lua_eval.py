#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("code", nargs="?", default="print('bridge lua probe'); return {frame=emu.framecount()}")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.lua_status())
        print(f.lua_eval(args.code))


if __name__ == "__main__":
    main()
