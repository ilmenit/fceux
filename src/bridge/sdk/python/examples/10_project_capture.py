#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge, Project


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("project_dir")
    parser.add_argument("--frames", type=int, default=1)
    args = parser.parse_args()

    project = Project.open(args.project_dir)
    with FceuxBridge.from_token_file(args.token_file) as f:
        project.capture_rom_metadata(f)
        project.add_note("Initial bridge project capture", tags=["capture"])
        project.add_label(0x8000, "entry_guess")
        project.add_comment(0x8000, "Initial entry-point placeholder; verify with reset vector/history.")
        f.frame(args.frames)
        project.capture_rawscreen(f, "screen-after-frame")
        project.capture_memory(f, 0x0000, 0x0800, "cpu-ram")
        project.capture_history(f, "recent-history", count=128)
        print(project.path)


if __name__ == "__main__":
    main()
