#!/usr/bin/env python3
import argparse

from fceux_bridge import FceuxBridge


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        ppu = f.ppu_peek(0x2000, 32)
        oam = f.oam_dump()
        palette = f.palette_dump()

        print("PPU $2000:", ppu["bytes"].hex(" "))
        print("OAM first sprite:", oam["bytes"][:4].hex(" "))
        print("Palette:", palette["palram_bytes"].hex(" "))


if __name__ == "__main__":
    main()
