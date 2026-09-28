# FCEUX Bridge Protocol

The initial bridge protocol is line-oriented:

- The server listens on a local TCP endpoint, POSIX Unix-domain socket, or Linux abstract Unix-domain socket.
- `stdio` is also supported for launched bridge-headless processes; the launcher owns the child stdin/stdout pipes.
- The token file contains the endpoint on the first line and the session token on the second line.
- The first client command must be `HELLO <token>`.
- Each command is one UTF-8 line.
- Each response is one JSON object line.
- Clients must wait for a response before sending the next command.
- `--bridge-headless` starts the same protocol without constructing the normal Qt console window, which is useful for CI and agent workflows that do not need the GUI.

Supported endpoints:

- `tcp:127.0.0.1:PORT`
- `unix:/path/to/socket`
- `unix-abstract:NAME`
- `stdio`

- `tcp:127.0.0.1:PORT`
- `tcp:127.0.0.1:0` for an ephemeral loopback TCP port
- `unix:/path/to/socket` on POSIX systems
- `unix-abstract:NAME` on Linux

Initial commands:

```text
HELLO <token>
PING
STATUS
PAUSE
RESUME
REGS
REG_SET register value
HISTORY [count] [include_disasm=true|false]
HISTORY_CLEAR
HISTORY_CONFIG [size=N] [enabled=true|false]
TRACE_START
TRACE_STOP
TRACE_STATUS
BP_SET addr [end=...] [domain=cpu|ppu|oam|rom] [mode=x|r|w|rw] [condition=...] [name=...] [enabled=false]
WATCH_SET addr [domain=cpu|ppu|oam|rom] [mode=r|w|rw] [condition=...] [name=...] [enabled=false]
BP_LIST
BP_CLEAR id
BP_CLEAR_ALL
BREAK_STATUS
RUN_UNTIL break [timeout_frames=N]
RUN_UNTIL pc=ADDR [timeout_frames=N]
RUN_UNTIL frame=N [timeout_frames=M]
STEP [count] [timeout_frames=N]
STEP_OVER [timeout_frames=N]
STEP_OUT [timeout_frames=N]
DISASM addr [count]
CALLSTACK [count]
SYM_LOAD path [bank=N]
SYM_LOAD auto=true
SYM_RESOLVE name [bank=N]
SYM_LOOKUP addr [bank=N]
LUA_EVAL "code"
LUA_LOAD path
LUA_RESET
LUA_STATUS
FRAME [count]
WAIT
PEEK addr [len]
PEEK16 addr
BUSPEEK addr [len]
BUSPEEK16 addr
MEMDUMP addr len
MEMSEARCH pattern [domain=cpu|ppu|rom] [start=ADDR] [end=ADDR] [limit=N]
CART_INFO
BANK_INFO [addr]
MEMMAP
ROM_PEEK offset len [domain=prg|chr]
ROM_DUMP [domain=prg|chr]
POKE addr value
POKE16 addr value
MEMLOAD addr base64:...
PPU_PEEK addr [len]
PPU_POKE addr value
PPU_DUMP [domain=pattern|nametable|palette|all] [start=ADDR] [len=N]
PPU_STATE
APU_STATE
OAM_DUMP
OAM_POKE addr value
PALETTE_DUMP
CDLOG_START
CDLOG_STOP
CDLOG_DUMP [domain=cpu|ppu|all]
STATE_SAVE [slot=...] [path=...] [inline=true]
STATE_LOAD slot=... | path=... | data=base64:...
STATE_LIST
STATE_DROP slot=... | all=true
JOY port [buttons...]
JOY_CLEAR [port]
INPUT_STATE
RAWSCREEN [overlay=true|false] [inline=true|false] [path=FILE]
SCREENSHOT [overlay=true|false] [inline=true|false] [path=FILE]
LOAD_ROM path
RESET
POWER
APP_EXIT
QUIT
```
