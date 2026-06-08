# FCEUX Bridge Python SDK

The initial SDK is dependency-free and lives under `bridge_sdk/python`.
It connects to `tcp:`, POSIX `unix:`, and Linux `unix-abstract:` endpoints from the bridge token file.

Example:

```python
from fceux_bridge import FceuxBridge

with FceuxBridge.from_token_file("/tmp/fceux-bridge-12345.token") as f:
    print(f.ping())
    print(f.status())
    print(f.regs())
    print(f.reg_set("pc", "$8000"))
    print(f.history_config(size=8192, enabled=True))
    f.frame(1)
    print(f.history(16))
    print(f.history(16, include_disasm=False))
    bp = f.bp_set(0x8000, mode="x", name="entry")["breakpoint"]
    print(f.bp_list())
    print(f.run_until_break(timeout_frames=600))
    print(f.step())
    print(f.step_over())
    print(f.disasm(0x8000, 8))
    print(f.callstack(8))
    print(f.sym_load(auto=True))
    print(f.sym_load("game.nes.0.nl", bank=0))
    print(f.sym_resolve("main"))
    print(f.sym_lookup(0x8000))
    print(f.lua_eval("print('probe'); return {byte=memory.readbyte(0)}"))
    print(f.lua_load("helpers.lua"))
    print(f.lua_reset())
    f.bp_clear(bp["id"])
    print(f.peek(0x0000))
    print(f.buspeek(0x2002))
    print(f.memsearch("ad ?? 20", domain="rom", limit=8))
    print(f.cart_info())
    print(f.bank_info(0x8000))
    print(f.memmap())
    print(f.rom_peek(0, 16, domain="prg"))
    print(f.ppu_peek(0x2000, 16))
    print(f.ppu_dump(domain="nametable", length=0x400))
    print(f.ppu_state())
    print(f.apu_state())
    print(f.cdlog_start())
    f.frame(1)
    print(f.cdlog_dump(domain="all"))
    print(f.cdlog_stop())
    print(f.oam_dump())
    print(f.palette_dump())
    print(f.state_save(path="/tmp/fceux-probe.fcs", inline=True))
    print(f.state_load(slot=None, path="/tmp/fceux-probe.fcs"))
    f.press(0, start=True, frames=1)
    with f.hold(0, "right"):
        f.frame(15)
    screen = f.rawscreen()
    f.screenshot("title.png", inline=False)
    with f.checkpoint():
        f.poke(0x0000, 1)
```

Project artifacts:

```python
from fceux_bridge import FceuxBridge, Project

project = Project.open("reverse-project")
with FceuxBridge.from_token_file("/tmp/fceux-bridge-12345.token") as f:
    project.capture_rom_metadata(f)
    project.add_label(0x8000, "entry_guess")
    project.add_comment(0x8000, "Verify against reset vector and history.")
    project.capture_rawscreen(f, "title")
    project.capture_memory(f, 0x0000, 0x0800, "cpu-ram")
    project.capture_history(f, "startup-history", count=256)
```

`Project` writes a human-readable `project.json` with labels, comments, regions, notes, findings, source metadata, and artifact references. Binary artifacts are stored in stable subdirectories such as `screenshots/`, `memory/`, `history/`, `traces/`, and `rom/`.

Use `FceuxBridge.command("RAW COMMAND")` for commands that do not yet have a typed helper.
Use `FceuxBridge.command_raw("RAW COMMAND")` when an expected error response should be inspected as JSON instead of raised as `FceuxBridgeError`.

SDK unit tests:

```bash
PYTHONPATH=bridge_sdk/python python -m unittest discover -s bridge_sdk/python/tests
```

These dependency-free tests cover client-side quoting, token-file validation, stdio-style command writes, structured error handling, close callbacks, `MEMSEARCH` pattern formatting, state save/load command construction, base64 response decoding, joypad helper cleanup, and `APP_EXIT` command construction.

Runtime smoke:

```bash
PYTHONPATH=bridge_sdk/python python examples/bridge/18_runtime_smoke.py \
  --fceux /tmp/fceux-bridge-build/src/fceux \
  --bridge-headless \
  --bridge=stdio \
  --sdl-dummy
```

The smoke runner generates a tiny NROM test ROM when no ROM is supplied, waits for the bridge token file, then checks connection, bad-token auth failure, tokenizer errors, decimal/`0x`/`$` numeric parsing, quoted option parsing, idle `WAIT`, trace start/stop/status aliases, frame advancement, registers, memory decode, inline and file-backed state restore, CPU/ROM/PPU `MEMSEARCH`, history entries and enable/disable behavior, rawscreen metadata, cart/bank/memory-map metadata, PRG/CHR `ROM_PEEK` and `ROM_DUMP`, CD log start/dump/stop buffers, direct PPU nametable/OAM/palette dump shapes, `SCREENSHOT path=...` PNG writing, and launched-process shutdown through `APP_EXIT`. The main launch path exercises `--bridge=stdio`; the bad-token child launch exercises `--bridge stdio`. Generated ROM, state, screenshot, and symbol paths intentionally include spaces to exercise SDK quoting and bridge command tokenization.

Debugger/Lua smoke:

```bash
PYTHONPATH=bridge_sdk/python python examples/bridge/18_runtime_smoke.py \
  --mode all \
  --fceux /tmp/fceux-bridge-build/src/fceux \
  --bridge-headless \
  --bridge=stdio \
  --sdl-dummy
```

`examples/bridge/19_debugger_lua_smoke.py` is a shortcut for `--mode debugger-lua`. It validates execute breakpoints, CPU write watchpoints, `REG_SET`, `RUN_UNTIL break`, structured `RUN_UNTIL` timeout errors, `BREAK_STATUS`, disassembly/history at the hit, single-step, step-over, step-out, Lua eval persistence/output capture, structured Lua eval errors, and the `emu.frameadvance()`/yield error path used to keep bridge eval non-yielding and session-safe.

Input smoke:

```bash
PYTHONPATH=bridge_sdk/python python examples/bridge/20_input_smoke.py \
  --fceux /tmp/fceux-bridge-build/src/fceux \
  --bridge-headless \
  --bridge=stdio \
  --sdl-dummy
```

The generated ROM samples joypad 1 through `$4016`, so this verifies `press()`, `hold()`, `JOY_CLEAR`, and `INPUT_STATE` against RAM counters changed by emulated code.
