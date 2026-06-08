# FCEUX Bridge Implementation Plan

## Goal

Add an AltirraBridge-like automation bridge to FCEUX so external agents can keep an emulator session alive while iteratively developing NES software, testing ROMs, inspecting failures, and reverse engineering existing programs.

The primary user is an LLM agent or automation script. The bridge must support short iterative operations:

- boot or reload a ROM
- run a deterministic number of frames
- inject controller input
- read/write CPU, PPU, OAM, palette, ROM, and mapper-visible state
- capture screenshots and raw frames
- inspect CPU registers and recent execution history
- set breakpoints/watchpoints
- checkpoint, probe, and rewind
- run small Lua snippets against the current emulator state

## Summary Recommendation

Build a native bridge with a Python SDK. Expose Lua through the bridge, but do not make Lua the transport.

Lua is useful as an in-emulator command surface, but a Lua-only bridge would inherit coroutine/frame-boundary constraints, platform-specific LuaSocket availability, and limited debugger access. A native bridge can expose the debugger, trace callbacks, save states, frame gates, and screen buffers directly while still letting agents execute Lua snippets when convenient.

For the first Qt implementation, use the Qt networking stack already present in the build (`QTcpServer`/`QTcpSocket`, and `QLocalServer` where available) behind a small bridge transport interface. A later headless server can reuse the bridge controller and replace only the transport/event-loop adapter.

## Design Principles

- Keep the emulator process stateful. Clients should connect, issue many small commands, and disconnect without restarting FCEUX.
- Keep the protocol simple. One command line in, one JSON object out.
- Keep execution deterministic. `FRAME n` must advance exactly `n` frames and re-pause.
- Keep command execution serialized. No pipelining and no concurrent core mutation.
- Keep transport local. Bind only to loopback or Unix domain sockets.
- Prefer FCEUX-native debug/core APIs over reimplementing emulator logic in client code.
- Keep Python SDK dependency-free at first. Use only the Python standard library.
- Make reverse-engineering artifacts persistent and scriptable: labels, comments, traces, screenshots, RAM dumps, and project metadata.

## Non-Goals For Initial Releases

- No remote network exposure.
- No server-initiated event stream in v1.
- No replacement of existing Lua or Qt JavaScript scripting.
- No complete Altirra command-name parity. NES-specific domains should be explicit.
- No full CPU profiler in the first transport milestone.
- No headless server in the first commit unless it falls out naturally.
- No attempt to expose every mapper-internal variable in v1. Start with stable CPU/PPU/cart/debug domains.

## Existing FCEUX Building Blocks

FCEUX already has most of the emulator and debugger primitives needed for a bridge.

- One-frame emulation: `src/fceu.cpp`, `FCEUI_Emulate`.
- Pause/frame helpers: `src/fceu.cpp`, `FCEUI_SetEmulationPaused`, `FCEUI_FrameAdvance`.
- Qt emulator loop and mutex: `src/drivers/Qt/fceuWrapper.cpp`, `fceuWrapperUpdate`, `DoFun`; `src/drivers/Qt/fceuWrapper.h`, `FCEU_WRAPPER_LOCK`, `FCEU_CRITICAL_SECTION`.
- ROM loading path: `src/drivers/Qt/fceuWrapper.cpp`, `LoadGame`, `LoadGameFromLua`.
- Reset/power paths: `src/fceu.cpp`, `ResetNES`, `PowerNES`; Qt wrappers `fceuWrapperSoftReset`, `fceuWrapperHardReset`.
- Save states: `src/state.h`, `FCEUSS_SaveMS`, `FCEUSS_LoadFP`.
- CPU registers: `src/x6502.h`, global `X6502 X`, `src/x6502abbrev.h`, and existing Lua register access.
- Debugger-safe CPU memory reads: `src/debug.cpp`, `GetMem`.
- CPU bus writes: `src/fceu.h`, `BWrite[A](A, V)`.
- Breakpoints/watchpoints: `src/debug.h`, `watchpoint[]`, `NewBreak`; debugger state via `FCEUI_Debugger()`.
- Trace callback hook: `src/debug.cpp`, `FCEUI_TraceInstructionRegister`. It is called from `DebugCycle()` and receives only opcode bytes and size, so the bridge must snapshot CPU globals and derive optional fields itself.
- Disassembly: `src/asm.h`, `Disassemble`; Qt symbolic variant in `src/drivers/Qt/SymbolicDebug.*`.
- Symbols: `src/debugsymboltable.*`.
- PPU function pointers and state: `src/ppu.h`, `FFCEUX_PPURead`, `FFCEUX_PPUWrite`, `FCEUPPU_PeekAddress`, `PPU`.
- PPU/OAM/palette arrays: `src/debug.h`, `PALRAM`, `UPALRAM`, `SPRAM`; direct PPU/ROM viewer patterns in `src/drivers/Qt/HexEditor.cpp`.
- ROM/cart mapping helpers: `src/debug.h`, `GetPRGAddress`, `GetNesFileAddress`, `getBank`, `GetNesPRGPointer`, `GetNesCHRPointer`; cart metadata in `src/cart.h`.
- Framebuffer: `src/video.h`, `XBuf`, `XBackBuf`, `FCEUD_GetPalette`.
- Input merge point: `src/input.cpp`, `UpdateGP`, which already merges physical, Lua, and Qt JavaScript joypad input.
- Lua APIs: `src/lua-engine.cpp`, including memory, PPU read, joypad, save states, hooks, screenshots, debugger counters.
- Lua state access: `src/fceulua.h`, `FCEU_GetLuaState`, `FCEU_LoadLuaCode`. `FCEU_LoadLuaCode` reloads a file and resets the Lua engine; interactive eval needs a new Lua-engine helper.
- Qt command-line parsing: `src/drivers/common/configSys.cpp` supports `--option value`, not `--option=value` or optional values. `--bridge[=ADDR]` needs a small pre-parser or parser enhancement.
- Qt Network is already discovered and linked in `src/CMakeLists.txt`.

## Current Repository Layout

Current native bridge code:

```text
src/bridge/
  BridgeServer.h/.cpp        transports, sessions, token file, authentication, dispatch, frame gate
  BridgeApu.h/.cpp           APU register/channel state command responses
  BridgeCart.h/.cpp          cart metadata, bank/memory maps, and PRG/CHR ROM command responses
  BridgeCdLog.h/.cpp         code/data logger buffer setup, status, start/stop, and dump command responses
  BridgeCpu.h/.cpp           CPU register, register write, disassembly, and stack-scan command responses
  BridgeDebugger.h/.cpp      breakpoints, watchpoints, hit bookkeeping, and breakpoint command responses
  BridgeExecution.h/.cpp     step, step-over/out, and run-until command responses
  BridgeHistory.h/.cpp       CPU execution history ring, history config, and trace command responses
  BridgeInput.h/.cpp         bridge-owned input injection state and joypad command responses
  BridgeProtocol.h/.cpp      tokenization, numeric parsing, option lookup
  BridgeJson.h/.cpp          JSON string escaping and small JSON array helpers
  BridgeLifecycle.h/.cpp     status, pause/resume, frame/wait, ROM load, reset/power, and app-exit command responses
  BridgeLua.h/.cpp           Lua eval/load/reset/status command responses around lua-engine helpers
  BridgeMemory.h/.cpp        CPU/debug memory, CPU bus read/write, PEEK/POKE/MEMLOAD, and MEMSEARCH command responses
  BridgePpu.h/.cpp           PPU state/peek/poke/dump, OAM, and palette command responses
  BridgeScheduler.h/.cpp     pause-bit helpers and frame-gate wait/notify logic
  BridgeScreen.h/.cpp        raw screen RGBA expansion and screenshot/raw command responses
  BridgeStateStore.h/.cpp    in-memory save-state slots, state serialization, and state command responses
  BridgeSymbols.h/.cpp       symbol load, resolve, lookup, and symbol JSON responses
```

The first implementation has started splitting stable pieces out of `BridgeServer.cpp`. `BridgeServer.*` now focuses on transports, sessions, authentication, token-file setup, command tokenization entry, and dispatch. `BridgeProtocol.*` now owns pure protocol helpers, `BridgeJson.*` owns string escaping plus small array helpers, `BridgeApu.*` owns APU state command response handling, `BridgeCart.*` owns cart metadata, bank/memory map, and PRG/CHR ROM command response handling, `BridgeCdLog.*` owns code/data logger setup plus status/start/stop/dump command response handling, `BridgeCpu.*` owns CPU register, register write, disassembly, and stack-scan command response handling, `BridgeDebugger.*` owns breakpoint/watchpoint command response handling and hit bookkeeping, `BridgeExecution.*` owns step, step-over/out, and run-until command response handling, `BridgeHistory.*` owns execution history, history configuration, and trace command response handling, `BridgeInput.*` owns bridge-injected joypad state plus input command response handling, `BridgeLifecycle.*` owns status, pause/resume, frame/wait, ROM load, reset/power, and app-exit command response handling, `BridgeLua.*` owns Lua eval/load/reset/status response handling, `BridgeMemory.*` owns CPU/debug memory, CPU bus read/write, PEEK/POKE/MEMLOAD, and CPU/PPU/ROM byte-pattern search command response handling, `BridgePpu.*` owns PPU state/peek/poke/dump plus OAM/palette command response handling, `BridgeScheduler.*` owns pause-bit helpers and frame-gate wait/notify logic, `BridgeScreen.*` owns screen RGBA expansion plus screenshot/raw command response handling, `BridgeStateStore.*` owns in-memory state slots, save/load serialization, and state command response handling, and `BridgeSymbols.*` owns symbol load/resolve/lookup response handling. A reviewable upstream series should continue splitting it once behavior stabilizes:

```text
src/bridge/
  BridgeProtocol.h/.cpp      command tokenization, number parsing, option lookup, base64 helpers
  BridgeJson.h/.cpp          small JSON writer for server responses
  BridgeLifecycle.h/.cpp     lifecycle, status, frame-gate, ROM load, reset, and app-exit command responses
  BridgeApu.h/.cpp           APU register/channel state command responses
  BridgeCart.h/.cpp          cart metadata, bank maps, memory maps, and ROM command responses
  BridgeCdLog.h/.cpp         code/data logger control and dump command responses
  BridgeCpu.h/.cpp           CPU register, register write, disassembly, and stack-scan command responses
  BridgeController.h/.cpp    command dispatch and FCEUX core operations
  BridgeExecution.h/.cpp     step, step-over/out, and run-until controller helpers
  BridgeInput.h/.cpp         bridge-injected joypad state and input command responses
  BridgeMemory.h/.cpp        CPU/debug memory, CPU bus access, PEEK/POKE/MEMLOAD, and MEMSEARCH command responses
  BridgePpu.h/.cpp           PPU state, PPU memory, OAM, and palette command responses
  BridgeScheduler.h/.cpp     lock/frame-gate handoff between transport and emulator
  BridgeScreen.h/.cpp        raw screen and screenshot command responses
  BridgeStateStore.h/.cpp    in-memory save-state slots, serialization, and state command responses
  BridgeSymbols.h/.cpp       symbol loading, lookup, resolve, and JSON helpers
  BridgeLua.h/.cpp           Lua eval/load/reset/status command responses around exported lua-engine helpers
```

Python SDK:

```text
bridge_sdk/python/fceux_bridge/
  __init__.py
  client.py
  project.py
  loader.py
  analyzer.py
  asm_writer.py
examples/bridge/
  01_ping.py
  02_history.py
  03_memory_domains.py
  04_breakpoints.py
  05_lua_eval.py
  ...
  18_runtime_smoke.py
  19_debugger_lua_smoke.py
  20_input_smoke.py
```

Documentation:

```text
documentation/bridge/
  PROTOCOL.md
  COMMANDS.md
  PYTHON_SDK.md
  AGENT_WORKFLOWS.md
```

## Protocol

Use a line-oriented protocol modeled after AltirraBridge.

Transport:

- `tcp:127.0.0.1:PORT`
- `tcp:127.0.0.1:0` for ephemeral port
- `unix:/path/to/socket` on POSIX
- `unix-abstract:NAME` on Linux
- `stdio` for launched bridge-headless/CI processes where local socket binds are unavailable

Startup:

```text
fceux --bridge[=tcp:127.0.0.1:0] game.nes
fceux --bridge-headless --bridge[=tcp:127.0.0.1:0] game.nes
fceux --bridge-headless --bridge=stdio game.nes
```

Implementation detail:

- The user-facing syntax should support `--bridge`, `--bridge=ADDR`, `--bridge ADDR`, and `--bridge-headless`.
- The current FCEUX config parser only supports `--option value`. Add a bridge pre-parser before `g_config->parse(argc, argv)` or extend the config parser to support `--option=value` and optional values.
- The pre-parser must remove bridge arguments from the argv passed to `g_config->parse`, otherwise `--bridge` will be treated as an invalid option or the address as a ROM path.
- Implemented Qt bridge transports support loopback `tcp:`, POSIX `unix:/path/to/socket`, Linux `unix-abstract:NAME`, and `stdio`.
- Implemented `--bridge-headless` starts a bridge-only Qt event loop without constructing the normal Qt console window. It still uses the Qt wrapper/core initialization path, but skips the OpenGL-backed UI and drives emulation with a timer calling `fceuWrapperUpdate()`.
- Implemented `stdio` duplicates the original stdout for protocol writes and redirects later normal stdout logging to stderr so command responses remain machine-readable.

Server prints to stderr:

```text
[bridge] listening on tcp:127.0.0.1:54321
[bridge] token-file: /tmp/fceux-bridge-12345.token
```

Token file:

```text
tcp:127.0.0.1:54321
32-hex-session-token
```

Rules:

- First command must be `HELLO <token>`.
- Commands are one UTF-8 line.
- Responses are one JSON object line.
- Client sends one command and waits for one response.
- Empty lines are ignored.
- Arguments use space-separated tokens with shell-like quoted strings and backslash escapes. The Python SDK should hide quoting from most callers.
- Binary data uses base64 inline payloads or server-side `path=...`.
- Numeric args accept decimal, `0xNN`, and `$NN`.
- `QUIT` closes the bridge client session. Use an explicit `APP_EXIT` command if an agent needs to request emulator shutdown.

Example:

```text
> HELLO 9ec0...
< {"ok":true,"protocol":1,"server":"FCEUX","paused":true}
> LOAD_ROM /tmp/game.nes
< {"ok":true,"rom":"/tmp/game.nes"}
> FRAME 60
< {"ok":true,"frames":60,"gate_active":true}
> REGS
< {"ok":true,"PC":"$8123","A":"$00","X":"$04","Y":"$10","S":"$fd","P":"$24","flags":"--U--I--","frame":60}
```

## Threading And Scheduling

The bridge must not call emulator-core APIs from the socket thread directly.

Recommended Qt integration:

1. Qt transport accepts a client and reads commands on its owning thread.
2. Each command is converted to a `BridgeRequest`.
3. The request is dispatched to a bridge scheduler.
4. The scheduler executes core work under `FCEU_CRITICAL_SECTION` or queues it to a frame boundary.
5. The transport writes one JSON response when the request completes.

Command execution categories:

- Immediate read/write commands: acquire the wrapper lock with `FCEU_CRITICAL_SECTION`, perform operation, release.
- ROM load/reset commands: route through existing Qt/main-thread load/reset paths and report when complete.
- `FRAME n`: set a bridge frame gate, unpause, return immediately. Later stateful commands block until the gate releases.
- `STATUS nowait=true`: allowed to report an active frame gate without waiting.
- `WAIT`: explicit barrier command that blocks until any active frame gate completes.
- Long commands such as screenshots, memory dumps, and save states: execute under lock after gate release.

Frame gate:

- Bridge owns `bridgeFrameGateRemaining`.
- `FRAME n` sets the counter, clears only `EMULATIONPAUSED_PAUSED` through the bridge pause helper, and preserves other pause bits.
- The gate decrements only when a real emulated frame completes. Do not decrement merely because `fceuWrapperUpdate()` or `DoFun()` was called while paused.
- Prefer observing `currFrameCounter` advancement or adding an explicit bridge frame-complete hook after unpaused `FCEUI_Emulate`.
- When it reaches zero, bridge sets `EMULATIONPAUSED_PAUSED` again and wakes waiting bridge requests.
- `PAUSE` and `RESUME` cancel any active gate.

Pause semantics:

- `FCEUI_SetEmulationPaused(int)` overwrites the whole pause bitmask. Do not use it blindly for bridge resume.
- Add small bridge/core helpers that set or clear only `EMULATIONPAUSED_PAUSED`, preserving `EMULATIONPAUSED_TIMER`, `EMULATIONPAUSED_FA`, and `EMULATIONPAUSED_NETPLAY` unless the bridge command explicitly cancels a bridge gate.
- `PAUSE`, `RESUME`, and frame-gate completion must be idempotent.

## Command Roadmap

### Milestone 0: Build And Option Plumbing

- Add CMake option `ENABLE_BRIDGE` defaulting on for Qt builds.
- Add bridge source files to `src/CMakeLists.txt`; reuse the existing Qt Network dependency for the first transport.
- Add command-line support for `--bridge`, `--bridge=ADDR`, and `--bridge ADDR` using a pre-parser or config parser enhancement.
- Add bridge startup/shutdown hooks.
- Write a token file and print the endpoint/token-file path to stderr.
- Add local TCP loopback transport.
- Add `HELLO`, `PING`, `STATUS`, `QUIT`.

Acceptance:

- Python can connect via token file and call `ping()`.
- Bad token fails.
- Unauthenticated commands fail.
- `QUIT` closes the client session cleanly without exiting the emulator.
- Unknown bridge command-line forms do not break existing ROM loading or normal FCEUX options.

### Milestone 1: Deterministic Runtime Control

Commands:

```text
PAUSE
RESUME
WAIT
FRAME [n]
LOAD_ROM path
POWER
RESET
REGS
REG_SET register value
APP_EXIT
```

Implementation notes:

- `LOAD_ROM` should be synchronous from the client's perspective.
- `FRAME n` returns immediately at the protocol level, and later stateful commands observe post-gate state.
- The Python SDK should make `frame(n)` feel blocking for ordinary users by issuing a barrier internally when needed, while still allowing low-level nonblocking `FRAME`.
- `REGS` returns CPU registers, flags string, `currFrameCounter`, `total_instructions`, cycle count from `timestampbase + timestamp - total_cycles_base`, pause state, and whether a bridge frame gate is active.
- Implemented `REG_SET register value` sets `PC`, `A`, `X`, `Y`, `S`, or `P` and returns an updated register snapshot, giving agents a deterministic way to establish CPU state for debugger probes.
- `POWER` and `RESET` should use existing wrapper/core reset paths and clear bridge history by default.
- `APP_EXIT` calls `fceuWrapperRequestAppExit()` in the Qt build.

Acceptance:

- `frame(1); regs()` observes state after exactly one frame.
- Repeated `FRAME 1` on the same ROM and input sequence is deterministic.
- `PAUSE`/`RESUME` are idempotent.
- `FRAME 0` is rejected or treated as `WAIT`, with behavior documented.

### Milestone 2: Memory, Input, Save States, Rendering

Commands:

```text
PEEK addr [len]
PEEK16 addr
MEMDUMP addr len
POKE addr value
POKE16 addr value
MEMLOAD addr base64:...
PPU_PEEK addr [len]
PPU_POKE addr value
OAM_DUMP
OAM_POKE addr value
PALETTE_DUMP
JOY port buttons
JOY_CLEAR [port]
STATE_SAVE path=... | slot=... | inline=true
STATE_LOAD path=... | slot=... | data=...
STATE_LIST
STATE_DROP slot=... | all=true
SCREENSHOT inline=true | path=...
RAWSCREEN inline=true | path=...
```

Memory domains:

- CPU/debug view: use `GetMem` for read side-effect safety.
- CPU bus write: use `BWrite[A](A, V)` for hardware-visible writes.
- Implemented CPU bus reads with side effects as explicit `BUSPEEK` and `BUSPEEK16` commands, leaving default `PEEK`/`PEEK16` debugger-safe.
- PPU mapped view: use `FFCEUX_PPURead ? FFCEUX_PPURead : FFCEUX_PPURead_Default` and corresponding write fallback. This can trigger PPU hooks/CD logging but does not emulate CPU register latch behavior.
- Implemented `PPU_DUMP` direct dump view adapts the direct `VPage`/`vnapage`/palette logic from the Qt hex editor for side-effect-free nametable/pattern/palette dumps.
- OAM direct view: expose `SPRAM`.
- Palette RAM: expose both `PALRAM` and `UPALRAM`, preserving palette mirror behavior on writes.
- ROM file view: expose PRG/CHR file offsets using `PRGptr`, `CHRptr`, `PRGsize`, `CHRsize`, `GetNesPRGPointer`, and `GetNesCHRPointer` after CPU/PPU domains are stable.

Input:

- Add bridge-owned input state instead of reusing Lua globals.
- Merge bridge input in `UpdateGP` in `src/input.cpp`, after physical input and after Lua/Qt JavaScript input have been applied.
- Implemented bridge input is cleared on authenticated session disconnect, `QUIT`, and bridge shutdown to avoid leaking held virtual buttons into later agent sessions.
- Support buttons: `a`, `b`, `select`, `start`, `up`, `down`, `left`, `right`.
- Define hold semantics explicitly. Implemented `JOY port buttons` persists until the next `JOY` or `JOY_CLEAR`; the Python SDK provides `press(..., frames=N)` and scoped `hold(...)` convenience helpers using `JOY`, `FRAME`, and `JOY_CLEAR`.
- Preserve FCEUX's existing opposite-direction handling/configuration; do not silently normalize bridge input differently from host input.

Rendering:

- `RAWSCREEN` returns width, height, stride, pixel format, and base64 pixels unless `inline=false` is requested. `RAWSCREEN path=...` writes raw RGBA bytes.
- FCEUX buffers are 256x256; default output should be the visible NES image, normally 256x240, with metadata for rendered line range and origin.
- Prefer pre-overlay `XBackBuf` for emulator screen and `XBuf` for overlay-included screen.
- Use `FCEUD_GetPalette` to expand indexed pixels to RGBA/XRGB.
- Keep `SCREENSHOT` as a convenience file/PNG command; keep `RAWSCREEN` as the deterministic SDK/agent primitive. Implemented `SCREENSHOT path=...` writes PNG while preserving optional inline RGBA return data.

Save states:

- Implemented `STATE_SAVE` can write any combination of `slot=...`, `path=...`, and `inline=true`.
- Implemented `STATE_LOAD` accepts exactly one source: `slot=...`, `path=...`, or `data=base64:...`.

Acceptance:

- Agent can boot a ROM, press Start, advance 60 frames, and capture a nonblank screenshot.
- State slot checkpoint/rewind works without disk I/O.
- Memory dump round-trips exact bytes for CPU RAM.
- PPU/OAM/palette dumps do not disturb CPU/PPU read latches.

### Milestone 3: Execution History

Commands:

```text
HISTORY [count] [include_disasm=true|false]
HISTORY_CLEAR
HISTORY_CONFIG [size=N] [enabled=true|false]
TRACE_START
TRACE_STOP
TRACE_STATUS
```

Bridge history record:

```text
seq
frame
cycle
instruction
pc
a, x, y, s, p
opcode bytes
op size
disassembly text
effective address, if known
read/write mode, if known
pre-write value, if known
prg rom offset, if known
bank, if known
scanline/dot, if cheap
```

Implementation:

- Register a bridge trace callback with `FCEUI_TraceInstructionRegister`.
- The callback signature is `void (*)(uint8 *opcode, int size)`. It does not provide PC/registers/effective address directly.
- In the callback, snapshot `X.PC`, `X.A`, `X.X`, `X.Y`, `X.S`, `X.P`, `currFrameCounter`, `total_instructions`, and `timestampbase + timestamp - total_cycles_base`.
- Derive PRG ROM address and bank with `GetPRGAddress`/`getBank`.
- Implemented history entries include `frame`, `scanline`, `dot`, `bank`, and `prg_offset` in addition to CPU registers, opcode bytes, counters, and optional disassembly.
- Implemented best-effort `effective_addr`, `access`, and `pre_write` fields are derived from FCEUX opcode tables and `GetMem`, without calling into the UI trace logger buffer.
- Keep a fixed-size preallocated ring buffer. The callback runs on the emulation thread, so avoid allocation, blocking locks, and expensive disassembly in the hot path.
- Do not depend on the Qt Trace Logger UI buffer.
- Format disassembly lazily when `HISTORY include_disasm=true` or for bounded counts.
- Initial default capacity: 4096 records, configurable up to 131072 through `HISTORY_CONFIG`.
- Cap response count initially, e.g. `HISTORY 4096`, even when the backing ring is larger.
- This requires builds with `FCEUDEF_DEBUGGER`; current Qt CMake builds define it, but the bridge option should fail clearly if built without debugger support.

Acceptance:

- `HISTORY 32` returns the last 32 executed instructions oldest-first.
- History survives while emulator runs and is cleared on ROM load/reset unless configured otherwise.
- Breakpoint hit plus `HISTORY 200` gives enough context to diagnose the path into the hit.
- History capture overhead is measured with `HISTORY_CONFIG enabled=true` and `enabled=false`.

### Milestone 4: Debugger Control And Introspection

Commands:

```text
DISASM addr [count]
BP_SET addr [end=...] [condition=...] [domain=cpu|ppu|oam|rom] [mode=x|r|w|rw]
BP_CLEAR id
BP_CLEAR_ALL
BP_LIST
WATCH_SET addr mode=r|w|rw [domain=cpu|ppu|oam|rom]
STEP [count]
STEP_OVER
STEP_OUT
RUN_UNTIL break [timeout_frames=N]
RUN_UNTIL pc=$ADDR [timeout_frames=N]
RUN_UNTIL frame=N
CALLSTACK [count]
MEMSEARCH pattern [start=...] [end=...] [domain=cpu|rom|ppu] [limit=N]
SYM_LOAD path [bank=N]
SYM_LOAD auto=true
SYM_RESOLVE name [bank=N]
SYM_LOOKUP addr [bank=N]
```

Implementation notes:

- Use `NewBreak`, `watchpoint[]`, and the condition parser for breakpoint setup.
- Map bridge domains to existing FCEUX flags: CPU execute/read/write with `WP_X`, `WP_R`, `WP_W`; PPU register address with `BT_P`; sprite/OAM with `BT_S`; ROM address with `BT_R` where applicable.
- Keep bridge IDs stable and map them to FCEUX watchpoint slots. Slot 64 is reserved by FCEUX for step-over and must not be allocated for normal bridge breakpoints.
- Add bridge hit bookkeeping. The core `BreakHit()` currently pauses and calls `FCEUD_DebugBreakpoint(bp_num)` for the driver/UI. Add a small bridge notification hook or callback list so `RUN_UNTIL break` can return the hit type, FCEUX slot, and bridge breakpoint ID without scraping UI state.
- `STEP` can use `FCEUI_Debugger().step`; `STEP_OUT` can use `FCEUI_Debugger().stepout`; `STEP_OVER` can use the existing reserved step-over slot behavior.
- Implemented `STEP_OVER` uses reserved watchpoint slot 64 for `JSR` and falls back to a normal debugger step for non-call opcodes. The bridge rejects a second overlapping step-over and clears slot 64 on timeout. Implemented `STEP_OUT` follows FCEUX's existing `stepout`/`jsrcount` semantics and is verified with a generated subroutine and explicit 6502 stack setup.
- Implemented `CALLSTACK` is a best-effort 6502 stack scan. It returns plausible return-address pairs, resume PCs, and disassembly, and labels the source as `6502-stack-scan` because stale non-call stack data can appear. Full robust call-stack tracking can be history-derived later.
- `RUN_UNTIL` must include timeout handling so agents cannot hang indefinitely when a breakpoint never fires.
- `SYM_LOAD path` maps to FCEUX's existing ld65 `.dbg` loader and FCEUX `.nl` parser. Explicit `.nl` loads accept `bank=N`; when omitted, the bridge infers RAM from `.ram.nl` and PRG pages from `.<hex>.nl`. `SYM_LOAD auto=true` maps to FCEUX's normal game-symbol loader for ROM sidecars.
- Implemented `MEMSEARCH` searches CPU debug memory, mapped PPU memory, and PRG ROM bytes. Hex patterns support byte wildcards as `??`; side-effecting CPU bus search remains a later enhancement.

Acceptance:

- Set breakpoint at reset/game entry, resume, observe pause at hit.
- Set watchpoint on RAM address, run until hit, inspect history.
- Load symbols and see them in disassembly or lookup responses.
- `RUN_UNTIL break timeout_frames=600` returns a structured timeout error if nothing hits.

### Milestone 5: Lua Bridge Surface

Commands:

```text
LUA_EVAL code
LUA_LOAD path
LUA_RESET
LUA_STATUS
```

Design:

- Keep a persistent bridge Lua context when possible.
- Implemented `LUA_EVAL` and `LUA_LOAD` return JSON-compatible Lua values in `result`: nil, boolean, number, string, arrays, and maps. Multiple return values are returned as a JSON array.
- Implemented bridge eval captures `print()` output in `output` by temporarily replacing and restoring the global `print` function during protected eval.
- Do not require Lua snippets to call `emu.frameadvance`; frame control remains a bridge command.
- Do not implement interactive eval by writing temporary Lua files through `FCEU_LoadLuaCode`; that function stops/reinitializes Lua and is intended for script-file loading.
- Add exported Lua-engine helpers, for example `FCEU_LuaEnsureBridgeState()` and `FCEU_LuaEvalString(code, result, output, error)`, implemented inside `src/lua-engine.cpp` where the existing static Lua setup functions are accessible.
- Execute Lua eval only while paused or at a known frame boundary, under the same bridge serialization rules as memory/debug commands.
- Implemented eval uses protected calls with a traceback handler and stack cleanup on success and failure. Rejecting/yield-error code paths such as `emu.frameadvance()` remains later hardening.
- If sharing the existing global Lua state conflicts with user-loaded scripts, keep the command design but switch implementation to a separate bridge-owned Lua state initialized with the same FCEUX libraries.

Use cases:

- Agent runs quick FCEUX-native checks:

```lua
return {
  pc = memory.getregister("pc"),
  lives = memory.readbyte(0x75),
}
```

- Agent loads a reusable script that defines helper functions, then calls them through `LUA_EVAL`.
- Implemented `LUA_LOAD` evaluates helper files in the persistent bridge Lua state instead of using `FCEU_LoadLuaCode`, so it does not enter normal frame-advance script mode. `LUA_RESET` stops and clears the Lua state.

Acceptance:

- `LUA_EVAL "return memory.readbyte(0)"` returns an integer in `result`.
- Lua state persists between two eval calls.
- A Lua error returns `{"ok":false,"error":"...","traceback":"...","output":"..."}` without killing the bridge.

### Milestone 6: NES-Specific Deep Introspection

Commands:

```text
PPU_STATE
APU_STATE
INPUT_STATE
CART_INFO
BANK_INFO [addr]
MEMMAP
ROM_DUMP domain=prg|chr
ROM_PEEK offset len [domain=prg|chr]
CDLOG_START
CDLOG_STOP
CDLOG_DUMP
```

NES domains to expose:

- CPU memory map and current PRG bank mapping.
- PPU registers and latch state.
- PPU address, scroll, scanline, dot.
- OAM/sprite contents.
- Palette RAM.
- CHR mapping and CHR RAM/ROM state.
- PRG/CHR ROM file offsets and mapper number.
- APU register shadows and channel enable state.
- Controller strobe and current joy state.
- Code/data logger state.
- Mapper-specific state only after a stable generic `CART_INFO`, `BANK_INFO`, and `MEMMAP` surface exists.
- `CART_INFO`, `BANK_INFO`, `MEMMAP`, `ROM_PEEK`, and `ROM_DUMP` expose the first generic cart/ROM surface: mapper metadata, PRG/CHR chip sizes, CPU PRG mapping rows, PPU CHR mapping rows, and bounded PRG/CHR dumps. Mapper-specific registers remain later work.
- `PPU_STATE` and `APU_STATE` expose register/latch snapshots useful for failure reports. `CDLOG_START`, `CDLOG_STOP`, and `CDLOG_DUMP` expose the existing FCEUX code/data logger buffers for agent-driven coverage and reverse engineering.

Acceptance:

- Agent can identify which PRG bank backs a CPU address.
- Agent can dump nametable, pattern table, OAM, palette, and raw screen.
- Agent can inspect cart mapper/PRG/CHR metadata.

### Milestone 7: Agent Project Toolkit

Python SDK modules:

```text
fceux_bridge.Project
fceux_bridge.loader
fceux_bridge.asm_writer
fceux_bridge.analyzer
```

Start with a minimal project model:

- source ROM path and hash
- labels
- comments
- regions
- notes
- screenshots
- memory snapshots
- trace snapshots
- findings metadata

Initial workflows:

- boot ROM and capture title screen
- frame-step gameplay agent loop
- memory diff before/after action
- find score/lives/health address
- set watchpoint and capture history around hit
- export disassembly with labels/comments
- smoke-test rebuilt homebrew ROM

Acceptance:

- Example scripts are reproducible from a token file.
- Project JSON is human-readable and git-friendly.
- Agent can save a trace/history capture and attach labels/comments to addresses.

Implemented first slice:

- `fceux_bridge.Project` stores human-readable `project.json` plus artifact files.
- `fceux_bridge.loader` provides small helpers for opening bridge/project sessions.
- `fceux_bridge.analyzer` provides byte-diff helpers for memory-change workflows.
- `fceux_bridge.asm_writer` exports disassembly text with project labels/comments.
- `fceux_bridge.smoke` can generate a tiny deterministic NROM ROM, launch FCEUX with `--bridge`, connect through the SDK, and exercise ping/status/frame/register/history/memory/state/rawscreen commands.
- The Python client exposes `command_raw()` for protocol-level tests and agent diagnostics that need the full JSON response for expected errors instead of raising `FceuxBridgeError`.
- `fceux_bridge.smoke` also provides a debugger/Lua smoke mode that sets deterministic execute breakpoints and CPU write watchpoints in the generated ROM, validates `RUN_UNTIL break`, `BREAK_STATUS`, `HISTORY include_disasm=true`, `STEP`, `DISASM`, and persistent `LUA_EVAL` state/output capture.
- The debugger/Lua smoke also validates the structured `RUN_UNTIL break timeout_frames=N` error path and a protected `LUA_EVAL` error with captured output, so timeout/error behavior is exercised without killing the bridge session.
- The generated NROM ROM also samples joypad 1 through `$4016` and increments RAM counters for A and Right, allowing `JOY`, `press()`, `hold()`, `JOY_CLEAR`, and `INPUT_STATE` to be verified against emulated software state instead of only protocol echoes.
- Example `10_project_capture.py` captures ROM metadata, labels/comments, raw screen, RAM, and history from a token file.
- Example `18_runtime_smoke.py` wraps the reusable smoke runner for manual or CI execution.
- Example `19_debugger_lua_smoke.py` wraps the debugger/Lua smoke mode.
- Example `20_input_smoke.py` wraps the input smoke mode.

### Milestone 8: Headless Server

Target:

```text
FCEUXBridgeServer --bridge=tcp:127.0.0.1:0 game.nes
```

Options:

- `--no-sound`
- `--no-video-window`
- `--pal`, `--ntsc`, `--dendy`
- `--newppu`
- `--config key=value`

Implementation paths:

1. Keep Qt bridge as the first working target, then extract common bridge/core modules for a server.
2. Create a minimal driver around `FCEUI_Initialize`, `FCEUI_LoadGame`, and one-frame emulation.
3. Reuse SDL pieces only where they are needed for input/video/audio initialization.

Current-state notes:

- Qt `--no-gui` is explicitly rejected in `fceuWrapperPreInit`, so headless is not a small command-line option change.
- Existing `fceux-server` code is already in the tree, but it should be treated as separate until verified for emulator-core bridge reuse.
- Headless should not depend on a visible Qt `consoleWindow`, `BlitScreen`, or host input polling.
- Implemented first headless slice: `--bridge-headless` removes the initial ROM from normal GUI parsing, starts the bridge before SDL/video initialization, initializes the Qt wrapper without constructing `consoleWindow`, loads the initial ROM through the bridge-capable wrapper path, and runs emulation from a Qt timer. A future dedicated server can replace the remaining Qt/SDL wrapper dependencies.

Acceptance:

- CI script starts server, reads token, boots ROM, advances frames, screenshots, quits.
- No GUI window required.
- Same Python SDK works for Qt and headless server.

## Python SDK Shape

Example client:

```python
from fceux_bridge import FceuxBridge

with FceuxBridge.from_token_file("/tmp/fceux-bridge-12345.token") as f:
    f.load_rom("game.nes")
    f.frame(120)  # SDK waits for the gate by default.
    f.press(0, start=True, frames=30)
    print(f.regs())
    print(f.history(20))
    f.screenshot("title.png")
```

Checkpoint pattern:

```python
with f.checkpoint() as cp:
    f.poke(0x75, 9)
    f.frame(60)
    print(f.regs())
# Auto-rewound.
```

History around watchpoint:

```python
f.watch_set(0x75, mode="w")
f.resume()
f.run_until_break(timeout_frames=600)
for h in f.history(80):
    print(h["pc"], h["op"], h.get("disasm"))
```

Lua:

```python
f.lua_eval("""
counter = (counter or 0) + 1
return {counter=counter, pc=memory.getregister("pc")}
""")
```

## Testing Strategy

Unit tests:

- number parser: decimal, `0x`, `$`
- command tokenization
- quoted argument and path parsing
- JSON escaping
- base64 encode/decode
- token file format
- history ring wraparound
- state slot store
- bridge command-line pre-parser: `--bridge`, `--bridge=ADDR`, `--bridge ADDR`, normal ROM paths
- pause helper bit preservation

Integration tests:

- start FCEUX with `--bridge`
- connect with Python client
- auth success/failure
- boot known test ROM
- frame-gate determinism
- memory read/write
- PPU/OAM/palette direct dump does not perturb state
- state save/load slot
- screenshot nonblank
- history returns expected shape
- breakpoint/watchpoint hit
- Lua eval persistence
- Lua eval error returns structured error and does not kill Lua or the bridge
- `RUN_UNTIL break` timeout returns without hanging

Implemented smoke coverage:

- `PYTHONPATH=bridge_sdk/python python -m unittest discover -s bridge_sdk/python/tests` validates dependency-free Python SDK behavior for command quoting, token-file validation, stdio-style command writes, structured errors, close callbacks, `MEMSEARCH` pattern formatting, `REG_SET`, history/frame/run/step/disassembly/callstack command construction, breakpoint/watchpoint command construction, symbol/Lua command construction, state save/load command construction, deep-introspection base64 response decoding, joypad helper cleanup, and `APP_EXIT` command construction.
- `python examples/bridge/18_runtime_smoke.py --fceux /path/to/fceux --bridge-headless --bridge=stdio --sdl-dummy` generates a tiny NROM counter ROM and validates bridge connection, bad-token auth failure, command tokenizer errors, decimal/`0x`/`$` numeric parsing, quoted option parsing, loaded-game status, idle `WAIT`, trace start/stop/status aliases, deterministic frame gate completion, register shape, decoded CPU RAM dump, inline and file-backed state save/load round-trips, CPU/ROM/PPU `MEMSEARCH`, execution history entries and enable/disable behavior, rawscreen metadata, cart mapper/PRG/CHR metadata, `$8000` bank mapping, CPU/PPU memory-map rows, PRG/CHR `ROM_PEEK` and `ROM_DUMP`, CD log start/dump/stop buffers, direct PPU nametable/OAM/palette dump shapes, `SCREENSHOT path=...` PNG writing, and launched-process shutdown through `APP_EXIT`. This path has been run successfully in the restricted sandbox where TCP/Unix socket binds are unavailable.
- The smoke runner's main launched process exercises `--bridge=stdio`; the bad-token auth child process exercises the split `--bridge stdio` form, covering two bridge pre-parser command-line spellings in the same run.
- The generated ROM path, state path, screenshot path, and symbol sidecar paths intentionally include spaces, so the smoke path exercises SDK quoting and bridge command tokenization for host paths.
- The runtime smoke also verifies that `FRAME 0`, malformed quoted commands, dangling escapes, and invalid `REG_SET` requests return structured protocol errors and leave the bridge session usable.
- `python examples/bridge/18_runtime_smoke.py --mode all --fceux /path/to/fceux --bridge-headless --bridge=stdio --sdl-dummy` additionally validates execute breakpoint setup, CPU write watchpoint setup, `REG_SET`, `RUN_UNTIL break`, structured run-until timeout, break-status correlation by bridge breakpoint/watchpoint id, disassembly/history around the hit, single-step response shape, `STEP_OVER` on a generated `JSR`, `STEP_OUT` from a generated subroutine with explicit stack setup, Lua print capture, Lua structured errors, the `emu.frameadvance()`/yield error path, and Lua state persistence. This combined path has been run successfully with the generated NROM ROM.
- The same `--mode all` path now validates bridge input injection by using the generated ROM's `$4016` polling loop and RAM counters for A and Right button observations. `examples/bridge/20_input_smoke.py` runs only this input slice.
- `python examples/bridge/18_runtime_smoke.py --token-file /tmp/fceux-bridge-123.token --rom test.nes` can run the same assertions against an already running bridge session.
- `python examples/bridge/22_history_overhead.py --fceux /path/to/fceux --bridge-headless --bridge=stdio --sdl-dummy` launches a generated NROM ROM, alternates `HISTORY_CONFIG enabled=false` and `enabled=true`, advances fixed-size frame batches, and emits JSON with per-mode samples, mean milliseconds per frame, and enabled-vs-disabled overhead ratio/delta. It can also attach to an existing active session with `--token-file`.

Golden or fixture ROMs:

- tiny NROM test ROM that increments a RAM counter once per frame
- test ROM that writes a known address from a known routine
- test ROM with predictable controller input response

Manual tests:

- Qt GUI remains usable while bridge is connected.
- Host controller input and bridge input do not leave stuck buttons after disconnect.
- Existing Lua scripts still run.
- Existing debugger/trace logger still works.
- `--bridge` does not change behavior when omitted.
- Paths with spaces work through the Python SDK.

## Risks And Mitigations

Thread safety:

- Mitigation: all core operations execute under existing emulator mutex or at frame boundaries.

Pause flag clobbering:

- Mitigation: bridge pause/resume helpers modify only bridge-relevant pause bits and preserve timer/netplay/frame-advance state.

Frame-gate off-by-one behavior:

- Mitigation: write a test ROM with a frame counter and assert exact increments.

History overhead:

- Mitigation: compact fixed-size records; optional enable/disable; no allocation in trace callback; avoid formatting disassembly unless requested or cache only opcode/state first. `examples/bridge/22_history_overhead.py` provides a repeatable runtime measurement so release candidates can capture the overhead of default history settings against the current build.

Lua state conflicts:

- Mitigation: implement eval inside the Lua engine with protected calls and stack cleanup; start with explicit bridge Lua mode and document interactions with user-loaded scripts. Add isolation later if needed.

Input conflicts:

- Mitigation: bridge-owned input layer with disconnect cleanup, merged with host input in one place.

Command-line parser compatibility:

- Mitigation: add bridge-specific pre-parser tests before modifying normal config parsing.

PPU/memory side effects:

- Mitigation: keep debug/direct dump commands separate from explicit bus-side-effect commands.

JSON dependency:

- Mitigation: server emits JSON through a small writer. Client parses with Python `json`. Avoid adding a heavyweight dependency for v1.

Headless complexity:

- Mitigation: ship Qt bridge first, then extract common modules for headless.

## Landing Strategy

The current prototype is intentionally broad so the command shape can be exercised by agents. For upstreaming or long-term maintenance, split it into reviewable patches:

1. Transport and lifecycle: `ENABLE_BRIDGE`, `--bridge`, TCP/Unix socket listener, token file, `HELLO`, `PING`, `STATUS`, `QUIT`, `APP_EXIT`.
2. Runtime control: pause/resume bit preservation, `LOAD_ROM`, reset/power, `FRAME`/`WAIT`, in-memory state slots.
3. CPU memory and screen: `REGS`, `PEEK`, `PEEK16`, `MEMDUMP`, `POKE`, `POKE16`, `MEMLOAD`, `RAWSCREEN`.
4. Debugging: execution history, trace configuration, breakpoints/watchpoints, stepping, `RUN_UNTIL`, `DISASM`.
5. Symbols and reverse engineering state: ld65/FCEUX symbol loading, `MEMSEARCH`, cart/bank/memory-map, ROM, PPU/OAM/palette, APU, CD log commands.
6. Interactive scripting and SDK: Lua eval/load/reset, Python client helpers, project workspace capture/analysis helpers, examples and docs.
7. Hardening: runtime integration tests with a known ROM, parser tests, command timeout tests, history overhead measurements, and cleanup of prototype monolithic server code.

## Open Questions

- Should `LOAD_ROM` respond only after the ROM is fully loaded, or after the load request is queued? Recommendation: respond after load completes.
- Should `FRAME n` return immediately like AltirraBridge, or block until complete? Recommendation: match AltirraBridge and make the next command block on gate release.
- Should screenshots include overlays by default? Recommendation: default to emulator framebuffer without overlays, add `overlay=true` option.
- Should history be always on? Recommendation: on by default for bridge sessions, configurable capacity, disable with `HISTORY_CONFIG enabled=false`.
- Should the bridge use existing Lua state or a separate bridge Lua state? Recommendation: start with existing Lua API access if practical, but keep command design compatible with a separate persistent bridge Lua context.
- Should bridge input be persistent or one-frame by default? Recommendation: persistent until `JOY_CLEAR`, with SDK helpers for frame-limited presses.
- Should direct PPU/ROM writes be allowed by default? Recommendation: allow only in local authenticated sessions, require explicit domain names, and document side effects.
- Should Qt and headless share the exact same command-line syntax? Recommendation: yes, even if headless is implemented later.
