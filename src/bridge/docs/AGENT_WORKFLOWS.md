# FCEUX Bridge Agent Workflows

The current bridge slice supports connection/authentication, pause/resume, deterministic frame gates, CPU and mapped PPU memory access, OAM/palette dumps, in-memory save-state slots, joypad injection, raw framebuffer capture, register inspection, execution history, and native FCEUX breakpoint/watchpoint setup.

Useful loops for agents:

- Build a ROM, `LOAD_ROM`, `FRAME n`, inspect `STATUS`, `REGS`, `HISTORY`, and `RAWSCREEN`.
- Use `STATE_SAVE slot=probe` before a risky input sequence, then `STATE_LOAD slot=probe` to replay a hypothesis.
- Use SDK `press(..., frames=N)` or `hold(...)` for deterministic controller sequences. Use raw `JOY`, `FRAME`, and `JOY_CLEAR` when exact low-level control is needed.
- Use `MEMDUMP`/`PEEK` for debugger-safe RAM assertions after tests or crashes. Use `BUSPEEK` only when a hardware-visible CPU bus read and its side effects are intentional.
- Use `MEMSEARCH` to find byte sequences in CPU memory, mapped PPU memory, or PRG ROM before placing breakpoints or patching test data.
- Use `CART_INFO`, `BANK_INFO`, `MEMMAP`, and `ROM_PEEK` to relate CPU addresses back to PRG/CHR ROM bytes and mapper metadata.
- Use `PPU_STATE` and `APU_STATE` to capture register/latch context around rendering or audio failures.
- Use `CDLOG_START`, frame/run commands, and `CDLOG_DUMP` to separate executed code, referenced data, and still-unseen PRG/CHR bytes.
- Use `PPU_DUMP`, `OAM_DUMP`, and `PALETTE_DUMP` to correlate nametable/pattern/sprite/palette state with `RAWSCREEN`. Use `PPU_PEEK` only when mapped PPU read-hook behavior is desired.
- Use `BP_SET`, `WATCH_SET`, `RUN_UNTIL`, `BREAK_STATUS`, and `HISTORY` to inspect the code path into a breakpoint.
- Use `CALLSTACK` at breakpoints for a best-effort scan of stacked return addresses, then confirm suspicious frames against `HISTORY` and `DISASM`.
- Use `STEP`, `STEP_OVER`, and `STEP_OUT` after a breakpoint hit to walk the next instructions while keeping `HISTORY` as context.
- Use `DISASM` around a PC or breakpoint address before changing code or setting additional breakpoints.
- Use `SYM_LOAD`, `SYM_RESOLVE`, and `SYM_LOOKUP` when ld65 `.dbg` files or FCEUX `.nl` sidecars are available, so reports can mention labels instead of only raw addresses.
- Use `LUA_LOAD` once for reusable bridge helper functions, `LUA_EVAL` for compact in-emulator probes, and `LUA_RESET` to clear helper state/hooks between experiments.
- Use `Project.open(...)` to persist labels, comments, findings, screenshots, RAM dumps, history captures, CD logs, and ROM metadata in a git-friendly directory.
- Use `sdk/python/examples/18_runtime_smoke.py --bridge-headless --bridge=stdio` after bridge changes to validate the basic agent loop against a generated tiny NROM ROM without requiring local socket bind permissions.
- Use `sdk/python/examples/18_runtime_smoke.py --mode all` or `sdk/python/examples/19_debugger_lua_smoke.py` after debugger or Lua changes to validate breakpoints, `RUN_UNTIL`, history/disassembly at hits, stepping, and Lua eval persistence.
- Use `sdk/python/examples/20_input_smoke.py` after input changes to validate `press()`, `hold()`, `JOY_CLEAR`, and `INPUT_STATE` against a ROM that polls `$4016`.

Remaining major plan items are a richer project-analysis layer and a headless/server split.
