#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Run deterministic frame-limited bridge input.")
    parser.add_argument("token_file")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.press(0, start=True, frames=10))
        with f.hold(0, "right"):
            print(f.frame(30))
        print(f.input_state())


if __name__ == "__main__":
    main()
