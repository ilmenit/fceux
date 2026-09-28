#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        print(f.ping())
        print(f.status())


if __name__ == "__main__":
    main()
