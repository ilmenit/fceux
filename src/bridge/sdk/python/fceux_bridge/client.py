import json
import socket
import base64
import selectors


class FceuxBridgeError(RuntimeError):
    pass


class FceuxBridge:
    def __init__(self, endpoint, token, timeout=10.0, file=None, close_callback=None):
        self.endpoint = endpoint
        self.token = token
        self.timeout = timeout
        self.sock = None
        self.file = file
        self.close_callback = close_callback

    @classmethod
    def from_token_file(cls, path, timeout=10.0):
        with open(path, "r", encoding="utf-8") as f:
            endpoint = f.readline().strip()
            token = f.readline().strip()
        if not endpoint or not token:
            raise FceuxBridgeError(f"invalid token file: {path}")
        bridge = cls(endpoint, token, timeout=timeout)
        bridge.connect()
        return bridge

    @classmethod
    def from_process_stdio(cls, process, token, timeout=10.0):
        class ProcessPipe:
            def __init__(self):
                self.selector = selectors.DefaultSelector()
                self.selector.register(process.stdout, selectors.EVENT_READ)

            def write(self, data):
                process.stdin.write(data)

            def flush(self):
                process.stdin.flush()

            def readline(self):
                events = self.selector.select(timeout)
                if not events:
                    raise FceuxBridgeError(f"timed out waiting for stdio bridge response after {timeout:.1f}s")
                return process.stdout.readline()

            def close(self):
                try:
                    process.stdin.close()
                except Exception:
                    pass

        bridge = cls("stdio", token, timeout=timeout, file=ProcessPipe())
        bridge.command(f"HELLO {token}")
        return bridge

    def __enter__(self):
        if self.sock is None:
            self.connect()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def connect(self):
        if self.file is not None:
            return
        if self.endpoint.startswith("tcp:"):
            host_port = self.endpoint[4:]
            host, port_text = host_port.rsplit(":", 1)
            self.sock = socket.create_connection((host, int(port_text)), timeout=self.timeout)
        elif self.endpoint.startswith("unix:"):
            path = self.endpoint[5:]
            if not hasattr(socket, "AF_UNIX"):
                raise FceuxBridgeError("unix sockets are not supported on this platform")
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            self.sock.connect(path)
        elif self.endpoint.startswith("unix-abstract:"):
            name = self.endpoint[len("unix-abstract:"):]
            if not hasattr(socket, "AF_UNIX"):
                raise FceuxBridgeError("unix sockets are not supported on this platform")
            if not name:
                raise FceuxBridgeError(f"unsupported endpoint: {self.endpoint}")
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            self.sock.connect("\0" + name)
        else:
            raise FceuxBridgeError(f"unsupported endpoint: {self.endpoint}")
        self.file = self.sock.makefile("rwb")
        self.command(f"HELLO {self.token}")

    def close(self):
        if self.file is None:
            return
        try:
            self.command("QUIT")
        except Exception:
            pass
        try:
            self.file.close()
        finally:
            self.file = None
            if self.sock is not None:
                self.sock.close()
                self.sock = None
            if self.close_callback is not None:
                self.close_callback()

    def command(self, line):
        data = self.command_raw(line)
        if not data.get("ok", False):
            raise FceuxBridgeError(data.get("error", "bridge command failed"))
        return data

    def command_raw(self, line):
        if self.file is None:
            raise FceuxBridgeError("not connected")
        self.file.write(line.encode("utf-8") + b"\n")
        self.file.flush()
        response = self.file.readline()
        if not response:
            raise FceuxBridgeError("bridge closed connection")
        return json.loads(response.decode("utf-8"))

    def quote(self, value):
        text = str(value)
        if text and not any(ch.isspace() or ch in '\\"' for ch in text):
            return text
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'

    def ping(self):
        return self.command("PING")

    def status(self):
        return self.command("STATUS")

    def pause(self):
        return self.command("PAUSE")

    def resume(self):
        return self.command("RESUME")

    def regs(self):
        return self.command("REGS")

    def reg_set(self, register, value):
        return self.command(f"REG_SET {self.quote(str(register).upper())} {self.quote(value)}")

    def history(self, count=64, include_disasm=True):
        return self.command(f"HISTORY {int(count)} include_disasm={'true' if include_disasm else 'false'}")

    def history_clear(self):
        return self.command("HISTORY_CLEAR")

    def history_config(self, size=None, enabled=None):
        parts = ["HISTORY_CONFIG"]
        if size is not None:
            parts.append(f"size={int(size)}")
        if enabled is not None:
            parts.append(f"enabled={'true' if enabled else 'false'}")
        return self.command(" ".join(parts))

    def trace_start(self):
        return self.command("TRACE_START")

    def trace_stop(self):
        return self.command("TRACE_STOP")

    def trace_status(self):
        return self.command("TRACE_STATUS")

    def bp_set(self, addr, *, end=None, domain="cpu", mode="x", condition=None, name=None, enabled=True):
        parts = [f"BP_SET {addr}", f"domain={self.quote(domain)}", f"mode={self.quote(mode)}"]
        if end is not None:
            parts.append(f"end={end}")
        if condition is not None:
            parts.append(f"condition={self.quote(condition)}")
        if name is not None:
            parts.append(f"name={self.quote(name)}")
        if not enabled:
            parts.append("enabled=false")
        return self.command(" ".join(parts))

    def watch_set(self, addr, *, mode="rw", domain="cpu", condition=None, name=None, enabled=True):
        parts = [f"WATCH_SET {addr}", f"domain={self.quote(domain)}", f"mode={self.quote(mode)}"]
        if condition is not None:
            parts.append(f"condition={self.quote(condition)}")
        if name is not None:
            parts.append(f"name={self.quote(name)}")
        if not enabled:
            parts.append("enabled=false")
        return self.command(" ".join(parts))

    def bp_list(self):
        return self.command("BP_LIST")

    def bp_clear(self, bridge_id):
        return self.command(f"BP_CLEAR {int(bridge_id)}")

    def bp_clear_all(self):
        return self.command("BP_CLEAR_ALL")

    def break_status(self):
        return self.command("BREAK_STATUS")

    def run_until_break(self, timeout_frames=600):
        return self.command(f"RUN_UNTIL break timeout_frames={int(timeout_frames)}")

    def run_until_pc(self, addr, timeout_frames=600):
        return self.command(f"RUN_UNTIL pc={self.quote(addr)} timeout_frames={int(timeout_frames)}")

    def run_until_frame(self, frame, timeout_frames=600):
        return self.command(f"RUN_UNTIL frame={int(frame)} timeout_frames={int(timeout_frames)}")

    def step(self, count=1, timeout_frames=60):
        return self.command(f"STEP {int(count)} timeout_frames={int(timeout_frames)}")

    def step_over(self, timeout_frames=600):
        return self.command(f"STEP_OVER timeout_frames={int(timeout_frames)}")

    def step_out(self, timeout_frames=600):
        return self.command(f"STEP_OUT timeout_frames={int(timeout_frames)}")

    def disasm(self, addr, count=16):
        return self.command(f"DISASM {addr} {int(count)}")

    def callstack(self, count=16):
        return self.command(f"CALLSTACK {int(count)}")

    def sym_load(self, path=None, *, auto=False, bank=None):
        if auto:
            return self.command("SYM_LOAD auto=true")
        if path is None:
            raise FceuxBridgeError("sym_load requires path or auto=True")
        suffix = "" if bank is None else f" bank={int(bank)}"
        return self.command(f"SYM_LOAD {self.quote(path)}{suffix}")

    def sym_resolve(self, name, bank=None):
        if bank is None:
            return self.command(f"SYM_RESOLVE {self.quote(name)}")
        return self.command(f"SYM_RESOLVE {self.quote(name)} bank={int(bank)}")

    def sym_lookup(self, addr, bank=None):
        if bank is None:
            return self.command(f"SYM_LOOKUP {addr}")
        return self.command(f"SYM_LOOKUP {addr} bank={int(bank)}")

    def lua_eval(self, code):
        return self.command(f"LUA_EVAL {self.quote(code)}")

    def lua_load(self, path):
        return self.command(f"LUA_LOAD {self.quote(path)}")

    def lua_reset(self):
        return self.command("LUA_RESET")

    def lua_status(self):
        return self.command("LUA_STATUS")

    def frame(self, count=1, wait=True):
        response = self.command(f"FRAME {int(count)}")
        if wait:
            return self.wait()
        return response

    def wait(self):
        return self.command("WAIT")

    def load_rom(self, path):
        return self.command(f"LOAD_ROM {self.quote(path)}")

    def peek(self, addr, length=None):
        if length is None:
            return self.command(f"PEEK {addr}")
        return self.command(f"PEEK {addr} {int(length)}")

    def peek16(self, addr):
        return self.command(f"PEEK16 {addr}")

    def buspeek(self, addr, length=None):
        if length is None:
            return self.command(f"BUSPEEK {addr}")
        response = self.command(f"BUSPEEK {addr} {int(length)}")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def buspeek16(self, addr):
        return self.command(f"BUSPEEK16 {addr}")

    def ppu_peek(self, addr, length=None):
        if length is None:
            return self.command(f"PPU_PEEK {addr}")
        response = self.command(f"PPU_PEEK {addr} {int(length)}")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def ppu_poke(self, addr, value):
        return self.command(f"PPU_POKE {addr} {int(value)}")

    def ppu_dump(self, domain="all", start=None, length=None):
        parts = ["PPU_DUMP", f"domain={self.quote(domain)}"]
        if start is not None:
            parts.append(f"start={start}")
        if length is not None:
            parts.append(f"len={int(length)}")
        response = self.command(" ".join(parts))
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def memdump(self, addr, length):
        response = self.command(f"MEMDUMP {addr} {int(length)}")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def poke(self, addr, value):
        return self.command(f"POKE {addr} {int(value)}")

    def poke16(self, addr, value):
        return self.command(f"POKE16 {addr} {int(value)}")

    def memload(self, addr, data):
        payload = base64.b64encode(bytes(data)).decode("ascii")
        return self.command(f"MEMLOAD {addr} base64:{payload}")

    def memsearch(self, pattern, *, domain="cpu", start=None, end=None, limit=256):
        if isinstance(pattern, str):
            payload = pattern.replace(" ", "")
        else:
            parts = []
            for item in pattern:
                if item is None or item == "??" or item == "?":
                    parts.append("??")
                else:
                    parts.append(f"{int(item) & 0xFF:02x}")
            payload = "".join(parts)
        parts = [f"MEMSEARCH hex:{payload}", f"domain={self.quote(domain)}", f"limit={int(limit)}"]
        if start is not None:
            parts.append(f"start={start}")
        if end is not None:
            parts.append(f"end={end}")
        return self.command(" ".join(parts))

    def cart_info(self):
        return self.command("CART_INFO")

    def bank_info(self, addr=None):
        if addr is None:
            return self.command("BANK_INFO")
        return self.command(f"BANK_INFO {addr}")

    def memmap(self):
        return self.command("MEMMAP")

    def rom_peek(self, offset, length, domain="prg"):
        response = self.command(f"ROM_PEEK {offset} {int(length)} domain={self.quote(domain)}")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def rom_dump(self, domain="prg"):
        response = self.command(f"ROM_DUMP {self.quote(domain)}")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def ppu_state(self):
        return self.command("PPU_STATE")

    def apu_state(self):
        return self.command("APU_STATE")

    def cdlog_start(self):
        return self.command("CDLOG_START")

    def cdlog_stop(self):
        return self.command("CDLOG_STOP")

    def cdlog_dump(self, domain="all"):
        response = self.command(f"CDLOG_DUMP domain={self.quote(domain)}")
        for key in ("cpu", "ppu"):
            if response.get(key) is not None and "base64" in response[key]:
                response[key]["bytes"] = base64.b64decode(response[key]["base64"])
        return response

    def state_save(self, slot="checkpoint", *, path=None, inline=False):
        parts = ["STATE_SAVE"]
        if slot is not None:
            parts.append(f"slot={self.quote(slot)}")
        if path is not None:
            parts.append(f"path={self.quote(path)}")
        if inline:
            parts.append("inline=true")
        response = self.command(" ".join(parts))
        if response.get("base64") is not None:
            response["bytes_data"] = base64.b64decode(response["base64"])
        return response

    def state_load(self, slot="checkpoint", *, path=None, data=None):
        sources = [slot is not None, path is not None, data is not None]
        if sum(1 for source in sources if source) != 1:
            raise FceuxBridgeError("state_load requires exactly one source: slot, path, or data")
        if slot is not None:
            return self.command(f"STATE_LOAD slot={self.quote(slot)}")
        if path is not None:
            return self.command(f"STATE_LOAD path={self.quote(path)}")
        payload = base64.b64encode(bytes(data)).decode("ascii")
        return self.command(f"STATE_LOAD data=base64:{payload}")

    def state_list(self):
        return self.command("STATE_LIST")

    def state_drop(self, slot=None):
        if slot is None:
            return self.command("STATE_DROP all=true")
        return self.command(f"STATE_DROP slot={self.quote(slot)}")

    def joy(self, port, *buttons, **named_buttons):
        pressed = list(buttons)
        for name, enabled in named_buttons.items():
            if enabled:
                pressed.append(name)
        suffix = " ".join(self.quote(button) for button in pressed)
        if suffix:
            return self.command(f"JOY {int(port)} {suffix}")
        return self.command(f"JOY {int(port)}")

    def press(self, port, *buttons, frames=1, clear=True, **named_buttons):
        self.joy(port, *buttons, **named_buttons)
        try:
            return self.frame(frames)
        finally:
            if clear:
                self.joy_clear(port)

    def hold(self, port, *buttons, **named_buttons):
        bridge = self

        class Hold:
            def __enter__(self):
                bridge.joy(port, *buttons, **named_buttons)
                return self

            def __exit__(self, exc_type, exc, tb):
                bridge.joy_clear(port)

        return Hold()

    def joy_clear(self, port=None):
        if port is None:
            return self.command("JOY_CLEAR")
        return self.command(f"JOY_CLEAR {int(port)}")

    def input_state(self):
        return self.command("INPUT_STATE")

    def oam_dump(self):
        response = self.command("OAM_DUMP")
        response["bytes"] = base64.b64decode(response["base64"])
        return response

    def oam_poke(self, addr, value):
        return self.command(f"OAM_POKE {addr} {int(value)}")

    def palette_dump(self):
        response = self.command("PALETTE_DUMP")
        response["palram_bytes"] = base64.b64decode(response["palram_base64"])
        response["upalram_bytes"] = base64.b64decode(response["upalram_base64"])
        return response

    def rawscreen(self, overlay=False, path=None, inline=None):
        parts = ["RAWSCREEN", f"overlay={'true' if overlay else 'false'}"]
        if path is not None:
            parts.append(f"path={self.quote(path)}")
        if inline is not None:
            parts.append(f"inline={'true' if inline else 'false'}")
        response = self.command(" ".join(parts))
        if response.get("base64") is not None:
            response["pixels"] = base64.b64decode(response["base64"])
        return response

    def screenshot(self, path=None, overlay=False, inline=None):
        parts = ["SCREENSHOT", f"overlay={'true' if overlay else 'false'}"]
        if path is not None:
            parts.append(f"path={self.quote(path)}")
        if inline is not None:
            parts.append(f"inline={'true' if inline else 'false'}")
        response = self.command(" ".join(parts))
        if response.get("base64") is not None:
            response["pixels"] = base64.b64decode(response["base64"])
        return response

    def checkpoint(self, slot="checkpoint"):
        bridge = self

        class Checkpoint:
            def __enter__(self):
                bridge.state_save(slot)
                return self

            def __exit__(self, exc_type, exc, tb):
                bridge.state_load(slot)

        return Checkpoint()

    def reset(self):
        return self.command("RESET")

    def power(self):
        return self.command("POWER")

    def app_exit(self):
        return self.command("APP_EXIT")
