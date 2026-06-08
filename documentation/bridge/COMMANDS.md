# FCEUX Bridge Commands

Initial implemented commands:

- `HELLO <token>` authenticates the client.
- `PING` returns `{"ok":true,"pong":true}`.
- `STATUS` returns pause, loaded-game, frame, and emulator-cycle status.
- `PAUSE` sets the paused bit while preserving other pause flags.
- `RESUME` clears the paused bit while preserving other pause flags.
- `REGS` returns CPU registers and counters.
- `REG_SET register value` sets one CPU register (`PC`, `A`, `X`, `Y`, `S`, or `P`) and returns the updated register snapshot. `PC` accepts 16-bit values; the other registers accept 8-bit values.
- `HISTORY [count] [include_disasm=true|false]` returns the most recent CPU instruction snapshots, capped at 4096 returned entries.
- `HISTORY_CLEAR` clears the execution-history ring.
- `HISTORY_CONFIG [size=N] [enabled=true|false]` resizes and/or enables history capture. Size is capped at 131072 entries.
- `TRACE_START` enables execution-history capture.
- `TRACE_STOP` disables execution-history capture and clears the current ring.
- `TRACE_STATUS` returns history capture status, ring capacity, count, sequence, and callback registration state.
- `BP_SET addr [end=ADDR] [domain=cpu|ppu|oam|rom] [mode=x|r|w|rw] [condition=EXPR] [name=TEXT] [enabled=false]` creates a breakpoint/watchpoint.
- `WATCH_SET addr [domain=cpu|ppu|oam|rom] [mode=r|w|rw] [condition=EXPR] [name=TEXT] [enabled=false]` creates a watchpoint; default mode is `rw`.
- `BP_LIST` lists FCEUX watchpoints with bridge IDs.
- `BP_CLEAR id` removes one bridge-owned breakpoint.
- `BP_CLEAR_ALL` removes all current breakpoints/watchpoints.
- `BREAK_STATUS` returns the most recent FCEUX breakpoint hit observed by the bridge.
- `RUN_UNTIL break [timeout_frames=N]` resumes until any breakpoint hit or a frame timeout.
- `RUN_UNTIL pc=ADDR [timeout_frames=N]` installs a temporary CPU execute breakpoint and runs until it hits or times out.
- `RUN_UNTIL frame=N [timeout_frames=M]` runs until an absolute frame number, bounded by `timeout_frames`.
- `STEP [count] [timeout_frames=N]` executes one or more debugger single steps.
- `STEP_OVER [timeout_frames=N]` steps over a `JSR` by using FCEUX's reserved step-over watchpoint slot, or performs a single step for non-call opcodes.
- `STEP_OUT [timeout_frames=N]` uses FCEUX debugger step-out semantics and returns on the resulting step break or timeout.
- `DISASM addr [count]` disassembles CPU memory through FCEUX's debugger-safe memory view.
- `CALLSTACK [count]` returns a best-effort scan of 6502 stack return-address pairs, including stack byte addresses, return addresses, resume PCs, and disassembly at each resume PC.
- `SYM_LOAD path [bank=N]` loads an ld65 `.dbg` file or a FCEUX `.nl` file into FCEUX's debug symbol table. `.nl` files accept `bank=N`; if omitted, filenames ending in `.ram.nl` load as RAM bank `-1`, and filenames ending in `.<hex>.nl` load as that PRG page.
- `SYM_LOAD auto=true` clears and reloads FCEUX's normal game symbols, including `.nl` sidecar files located by the emulator.
- `SYM_RESOLVE name [bank=N]` resolves a symbol name to an address/offset. Banks `-1` and `-2` represent RAM and register-map pages.
- `SYM_LOOKUP addr [bank=N]` resolves an address to a symbol using the supplied bank, the current mapped PRG bank for addresses `$8000-$ffff`, or RAM/register pages for lower addresses.
- `LUA_EVAL "code"` evaluates Lua code in the persistent FCEUX Lua state and returns JSON-compatible Lua return values in `result`; captured `print()` output is returned in `output`.
- `LUA_LOAD path` reads a Lua file and evaluates it in the persistent bridge Lua state, useful for installing reusable helper functions. It returns `result`, `output`, and path metadata.
- `LUA_RESET` stops and clears the current Lua state.
- `LUA_STATUS` reports whether the Lua state is initialized/running and the current script name if one was loaded normally.
- `FRAME [count]` starts a deterministic frame gate and returns immediately.
- `WAIT` blocks until an active frame gate completes.
- `PEEK addr [len]` reads CPU memory through FCEUX debugger-safe `GetMem`.
- `PEEK16 addr` reads a little-endian 16-bit CPU memory value.
- `BUSPEEK addr [len]` reads CPU memory through the emulated CPU bus `ARead`, which can trigger mapper or hardware side effects.
- `BUSPEEK16 addr` reads a little-endian 16-bit CPU bus value through `ARead`.
- `MEMDUMP addr len` returns CPU memory as both an integer array and base64.
- `MEMSEARCH pattern [domain=cpu|ppu|rom] [start=ADDR] [end=ADDR] [limit=N]` searches for a byte pattern. Patterns are `hex:...`, plain hex, or `base64:...`; hex patterns support byte wildcards as `??`; `rom` searches PRG ROM bytes.
- `CART_INFO` returns current ROM/cart metadata, mapper number, hashes, PRG/CHR chip sizes, and save-RAM metadata.
- `BANK_INFO [addr]` returns PRG bank/file-offset information for one CPU address or the main mapped PRG windows.
- `MEMMAP` returns CPU 8 KiB mapping rows and PPU 1 KiB CHR mapping rows.
- `ROM_PEEK offset len [domain=prg|chr]` returns PRG or CHR ROM bytes as arrays and base64.
- `ROM_DUMP [domain=prg|chr]` returns a full PRG or CHR ROM dump, capped at 1 MiB per response.
- `POKE addr value` writes one byte through the CPU bus write handler.
- `POKE16 addr value` writes a little-endian 16-bit value through the CPU bus write handler.
- `MEMLOAD addr base64:...` writes bytes through the CPU bus write handler.
- `PPU_PEEK addr [len]` reads mapped PPU memory through FCEUX's PPU read hook, wrapping at `$3fff`.
- `PPU_POKE addr value` writes mapped PPU memory through FCEUX's PPU write hook.
- `PPU_DUMP [domain=pattern|nametable|palette|all] [start=ADDR] [len=N]` returns side-effect-free direct PPU memory bytes from `VPage`, `vnapage`, and palette RAM as arrays and base64.
- `PPU_STATE` returns PPU registers, current VRAM address/latches, scroll estimate, scanline/dot, and rendering flags.
- `APU_STATE` returns APU register shadows, DMC latches, and channel enable bits.
- `OAM_DUMP` returns the 256-byte sprite OAM buffer as both an integer array and base64.
- `OAM_POKE addr value` writes one byte in sprite OAM.
- `PALETTE_DUMP` returns `PALRAM` and `UPALRAM` as arrays and base64.
- `CDLOG_START` allocates/resets code-data logger buffers and enables FCEUX code/data logging.
- `CDLOG_STOP` disables code-data logging.
- `CDLOG_DUMP [domain=cpu|ppu|all]` returns logger counters and CPU/PPU code-data logger buffers as base64.
- `STATE_SAVE [slot=NAME] [path=FILE] [inline=true]` saves an emulator state to an in-memory slot, host file, and/or inline base64 response.
- `STATE_LOAD slot=NAME | path=FILE | data=base64:...` loads an emulator state from exactly one source.
- `STATE_LIST` lists in-memory state slots.
- `STATE_DROP slot=NAME` or `STATE_DROP all=true` removes state slots.
- `JOY port [buttons...]` sets persistent bridge-owned joypad buttons.
- `JOY_CLEAR [port]` clears one or all bridge-owned joypad masks.
- `INPUT_STATE` returns bridge-owned joypad masks.
- `RAWSCREEN [overlay=true|false] [inline=true|false] [path=FILE]` returns RGBA framebuffer bytes as base64 and/or writes raw RGBA bytes to `path`.
- `SCREENSHOT [overlay=true|false] [inline=true|false] [path=FILE]` returns RGBA framebuffer bytes as base64 and/or writes a PNG screenshot to `path`.
- `LOAD_ROM path` loads a ROM path synchronously.
- `RESET` performs a soft reset if a game is loaded.
- `POWER` performs a power cycle if a game is loaded.
- `APP_EXIT` requests application shutdown.
- `QUIT` closes the client session.

`HISTORY` entries include sequence number, frame, PPU scanline/dot, instruction counter, cycle counter, CPU PC, mapped PRG bank, PRG ROM offset when known, best-effort effective CPU address, access mode, pre-write value for writes, CPU registers, flags, opcode bytes, decoded size, and optional disassembly text. The ring is cleared on ROM load, reset, power cycle, bridge shutdown, `TRACE_STOP`, and history resize.

Breakpoint domains map to FCEUX debugger domains: CPU memory, mapped PPU memory, sprite OAM, and ROM addresses. Slot 64 remains reserved by FCEUX for step-over and is not used by bridge breakpoint allocation.

`CALLSTACK` is marked with `source:"6502-stack-scan"` because it reads plausible return-address pairs from the CPU stack page. It is useful breakpoint context, but it can include stale stack data when a game uses the stack for non-call data.

Lua bridge results convert nil, booleans, numbers, strings, array-like tables, and map-like tables to JSON. Multiple return values are returned as a JSON array. Lua errors return `ok:false` with `error`, `traceback`, and any captured `output`; the bridge restores the previous global `print` function after each eval.

`PPU_PEEK` uses the emulator's mapped PPU read path and may trigger hooks/logging. `PPU_DUMP` is the side-effect-free direct view intended for nametable, pattern table, palette, and whole-PPU snapshots.

`PEEK`, `PEEK16`, and `MEMDUMP` are debugger-safe CPU reads. `BUSPEEK` and `BUSPEEK16` are explicit side-effecting CPU bus reads for cases where hardware-visible behavior is desired.

Bridge input is persistent at the protocol level: `JOY` holds the specified buttons until another `JOY` for that port or `JOY_CLEAR`. The Python SDK provides `press(..., frames=N)` and `hold(...)` helpers for frame-limited input.
Authenticated session disconnect, `QUIT`, and bridge shutdown clear all bridge-owned joypad masks so virtual buttons do not leak into a later agent session.

`SYM_LOAD path` supports ld65 `.dbg` files and explicit FCEUX `.nl` files. `SYM_LOAD auto=true` remains the easiest way to load all `.nl` sidecars that match the currently loaded ROM path.

`examples/bridge/18_runtime_smoke.py` is the recommended quick regression check for the core command set. Prefer `--bridge-headless --bridge=stdio` when launching FCEUX from the smoke runner in CI or sandboxed environments. It launches FCEUX when given `--fceux`, or connects to an existing token file, then validates connection, frame, register, memory, state, history, and rawscreen behavior.

Use `examples/bridge/18_runtime_smoke.py --mode all` or `examples/bridge/19_debugger_lua_smoke.py` for the deeper debugger/Lua regression path covering breakpoints, `RUN_UNTIL`, hit status, stepping, disassembly/history, and persistent Lua eval.

Use `examples/bridge/20_input_smoke.py` to verify bridge joypad injection end to end. The generated ROM polls `$4016` and updates RAM counters, so this tests input as observed by emulated software, not only `JOY` command responses.
