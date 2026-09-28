#!/usr/bin/env python3
import argparse
import json

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Print best-effort bridge call stack context.")
    parser.add_argument("token_file")
    parser.add_argument("--count", type=int, default=8)
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(json.dumps(f.callstack(args.count), indent=2))


if __name__ == "__main__":
    main()
