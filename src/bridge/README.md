# FCEUX Bridge

Control the NES emulator from Python or any tool that can speak the local
Bridge protocol. Run the Qt frontend interactively or use `--bridge-headless`
for automation. The Bridge supports frame stepping, memory access, CPU
registers, debugger breakpoints, instruction history, input, screenshots,
savestates, Lua, and symbols.

## Download and package contents

```text
bin/fceux[.exe]          Emulator with Bridge enabled
bin/                    Windows runtime DLLs and Qt plugins
runtime/                Bundled Linux Qt libraries and plugins
licenses/               Linux library redistribution notices
sdk/python/fceux_bridge/ Dependency-free Python SDK
sdk/python/examples/    Runnable examples and regression tools
sdk/python/tests/       SDK unit tests
docs/                   Protocol, commands, SDK guide, agent workflows
palettes/               Emulator palettes
luaScripts/             Emulator Lua scripts
tools/                  Emulator tools
README.md               This guide
BUILD-INFO.txt          Source commit and workflow run
COPYING                 GPL license
```

The package includes the full emulator. It supports a headless mode but
uses bundled Qt libraries and, on Windows, bundled SDL runtime libraries.


Download the latest experimental build from:
https://github.com/ilmenit/fceux/releases/tag/nightly-bridge

The Windows x86_64 ZIP includes the Qt frontend and its runtime libraries.
Extract the whole archive, then run `bin/fceux.exe`. Windows 10 or later is
recommended. Keep the DLLs and Qt plugin folders beside the executable.
Lua and the emulator's zlib code are compiled in. Qt, SDL2, libarchive,
and MSVC runtime DLLs are bundled because the supplied Windows Qt/SDL
builds use shared libraries. No separate Qt or Lua installation is needed.

The Linux x86_64 tar.gz is built on Ubuntu 22.04 and requires glibc 2.35
or later. Lua is compiled into the emulator. SDL2, minizip, zlib, and
libarchive use static libraries; Qt and remaining non-system dependencies
are bundled in `runtime/`. Fully static Qt builds would require a separate
Qt toolchain and static GUI plugins; the package supplies those libraries
and plugins alongside the statically linked emulator dependencies.

Extract the whole archive and run `./bin/fceux`. This launcher selects the
bundled runtime and works from any directory. Do not move `bin/` away from
`runtime/`, and do not launch `runtime/usr/bin/fceux` directly. A normal
Linux desktop supplies the OS C/C++ runtime, graphics drivers, fonts, and
a Wayland or X11 display server. Native Wayland, X11, and offscreen Qt
platform plugins are included. Qt, Lua, SDL2,
minizip, and libarchive do not need to be installed separately.

CI tests the extracted Linux package in clean Ubuntu 22.04 and Fedora 44
containers without installing Qt, Lua, SDL2, minizip, or libarchive. Both
headless Bridge startup and GUI startup under a virtual X server are checked.
`LINKAGE.txt` records direct ELF dependencies; `RUNTIME-LIBRARIES.txt`
records the resolved Linux libraries.
Palettes, Lua scripts, and tools are included at the archive root.

## Start the Bridge

In a source checkout, run SDK commands below from `src/bridge` and use the
path to your built emulator instead of `./bin/fceux`.


From the extracted directory:

```sh
./bin/fceux --bridge=tcp:127.0.0.1:0
```

On Windows use `./bin/fceux.exe` instead. To run without the emulator window,
add `--bridge-headless`; a ROM path can follow the options. For unattended
Linux sessions set `QT_QPA_PLATFORM=offscreen` and `SDL_AUDIODRIVER=dummy`, and `SDL_VIDEODRIVER=dummy`.
The Bridge announces its token file on stderr. Use that path with the SDK:

```sh
export PYTHONPATH="$PWD/sdk/python"
python3 sdk/python/examples/01_ping.py /path/to/fceux-bridge.token
```

In Windows PowerShell:

```powershell
$env:PYTHONPATH = "$pwd/sdk/python"
python sdk/python/examples/01_ping.py C:/path/to/fceux-bridge.token
```

The SDK requires Python 3.9 or later. See `docs/PROTOCOL.md`,
`docs/COMMANDS.md`, and `docs/PYTHON_SDK.md` for details
The source repository uses the same layout under `src/bridge`.
`BUILD-INFO.txt` identifies the source commit. `SHA256SUMS.txt` is available
alongside release assets. The GPL license is included as `COPYING`; source
for each build is available through its recorded commit on GitHub.

## CI and publication

`.github/workflows/bridge-nightly.yml` builds pushes and pull requests targeting
`FceuxBridge`. Each build checks an authenticated Bridge ping and uploads
archives as Actions artifacts. Only successful builds of both platforms from
`ilmenit/fceux:FceuxBridge` update the rolling `nightly-bridge` prerelease.
Pull requests only produce artifacts. Publication uses `GITHUB_TOKEN` with
job-level `contents: write`; no additional secret is required.

Manual dispatch is also supported. GitHub requires the workflow to be present
on the repository's default branch for manual dispatch to be available.
There is no cron trigger: the nightly release refreshes on each branch push,
following AltirraSDL's rolling Bridge release pattern.

## Python quick start

Launch the emulator with your ROM and `--bridge=tcp:127.0.0.1:0`, then connect
using the announced token file:

```python
from fceux_bridge import FceuxBridge

with FceuxBridge.from_token_file("/path/to/fceux-bridge.token") as nes:
    print(nes.ping())
    nes.frame(60)
    print(nes.regs())
    nes.screenshot("screen.png", inline=False)
```

Set `PYTHONPATH` as above before running your script. Load a ROM before
commands that require a running game. See [the SDK guide](docs/PYTHON_SDK.md)
for connection, state, input, and project-capture examples.

## Build from source

From the repository root, configure with `-DENABLE_BRIDGE=ON` (the default):

```sh
cmake -S . -B build/bridge -DCMAKE_BUILD_TYPE=Release -DENABLE_BRIDGE=ON
cmake --build build/bridge --parallel 2
```

See the repository's `README` and the nightly workflow for platform build
dependencies. C++ implementation files remain directly in `src/bridge`, with
emulator integration in the core and Qt frontend. SDK, examples, and docs
live alongside the implementation and are copied into the release package.
