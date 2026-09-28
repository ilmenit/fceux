#!/usr/bin/env python3
import sys

from fceux_bridge.smoke import main


if __name__ == "__main__":
    main(["--mode", "symbols", *sys.argv[1:]])
