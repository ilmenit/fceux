#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Save bridge screenshot artifacts on the emulator host.")
    parser.add_argument("token_file")
    parser.add_argument("--png", default="/tmp/fceux-bridge-screen.png")
    parser.add_argument("--rgba", default="/tmp/fceux-bridge-screen.rgba")
    parser.add_argument("--overlay", action="store_true")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.screenshot(args.png, overlay=args.overlay, inline=False))
        print(f.rawscreen(path=args.rgba, overlay=args.overlay, inline=False))


if __name__ == "__main__":
    main()
