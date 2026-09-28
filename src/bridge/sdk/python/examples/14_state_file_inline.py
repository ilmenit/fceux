#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser(description="Exercise bridge state save/load file and inline forms.")
    parser.add_argument("token_file")
    parser.add_argument("--path", default="/tmp/fceux-bridge-state.fcs")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        saved = f.state_save(slot="probe", path=args.path, inline=True)
        print({"saved_bytes": saved["bytes"], "path": saved["path"], "inline": saved.get("base64") is not None})
        print(f.state_load(slot=None, path=args.path))
        print(f.state_load(slot=None, data=saved["bytes_data"]))


if __name__ == "__main__":
    main()
