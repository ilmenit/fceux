#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("addr")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        bp = f.bp_set(args.addr, mode="x", name="agent-breakpoint")["breakpoint"]
        print("set:", bp)
        print("list:", f.bp_list())
        print("run:", f.run_until_break(timeout_frames=600))
        print("step:", f.step())
        print("disasm:", f.disasm(args.addr, 4))
        print("last hit:", f.break_status())
        f.bp_clear(bp["id"])


if __name__ == "__main__":
    main()
