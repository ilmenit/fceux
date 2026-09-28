#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from fceux_bridge import FceuxBridge


HELPER = """
bridge_helpers = bridge_helpers or {}
function bridge_helpers.read_zero_page(addr)
  return memory.readbyte(addr)
end
return "helpers loaded"
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("token_file")
    parser.add_argument("--script", help="optional Lua helper file to load")
    args = parser.parse_args()

    with FceuxBridge.from_token_file(args.token_file) as f:
        if args.script:
            script = Path(args.script)
            print(f.lua_load(script))
        else:
            with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as tmp:
                tmp.write(HELPER)
                script = Path(tmp.name)
            print(f.lua_load(script))

        print(f.lua_eval("return bridge_helpers.read_zero_page(0)"))
        print(f.lua_status())
        print(f.lua_reset())


if __name__ == "__main__":
    main()
