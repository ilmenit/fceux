import argparse
import json
import os
import re
import selectors
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from .client import FceuxBridge, FceuxBridgeError


TOKEN_RE = re.compile(r"\[bridge\] token-file:\s*(?P<path>.+)")


def _text(data):
    if isinstance(data, bytes):
        return data.decode("utf-8", errors="replace")
    return data


def write_counter_rom(path):
    """Write a minimal iNES NROM ROM that increments RAM and samples joypad 1."""
    path = Path(path)
    prg = bytearray([0xEA] * 0x4000)
    code = bytes(
        [
            0x78,  # sei
            0xD8,  # cld
            0xA2,
            0xFF,  # ldx #$ff
            0x9A,  # txs
            0xA9,
            0x00,  # lda #$00
            0x85,
            0x00,  # sta $00
            0x85,
            0x01,  # sta $01
            0x85,
            0x02,  # sta $02
            0x85,
            0x03,  # sta $03
            0x85,
            0x04,  # sta $04
            0x85,
            0x05,  # sta $05
            0x85,
            0x06,  # sta $06
            0x85,
            0x07,  # sta $07
            0x85,
            0x08,  # sta $08
            0xE6,
            0x00,  # inc $00
            0x20,
            0x46,
            0x80,  # jsr $8046
            0xA9,
            0x01,  # lda #$01
            0x8D,
            0x16,
            0x40,  # sta $4016
            0xA9,
            0x00,  # lda #$00
            0x8D,
            0x16,
            0x40,  # sta $4016
            0xA2,
            0x08,  # ldx #$08
            0xAD,
            0x16,
            0x40,  # lda $4016
            0x4A,  # lsr a
            0x66,
            0x01,  # ror $01
            0xCA,  # dex
            0xD0,
            0xF7,  # bne read_loop
            0xA5,
            0x01,  # lda $01
            0x29,
            0x01,  # and #$01
            0xF0,
            0x02,  # beq +2
            0xE6,
            0x02,  # inc $02 ; A
            0xA5,
            0x01,  # lda $01
            0x29,
            0x80,  # and #$80
            0xF0,
            0x02,  # beq +2
            0xE6,
            0x03,  # inc $03 ; Right
            0x4C,
            0x19,
            0x80,  # jmp $8019
            0xE6,
            0x04,  # inc $04
            0x60,  # rts
        ]
    )
    prg[: len(code)] = code
    prg[0x3FFA:0x4000] = bytes([0x00, 0x80, 0x00, 0x80, 0x00, 0x80])
    chr_rom = bytes(0x2000)
    header = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)
    path.write_bytes(header + prg + chr_rom)
    return path


def write_symbol_files(rom_path):
    rom_path = Path(rom_path)
    prg_symbols = rom_path.with_name(rom_path.name + ".0.nl")
    ram_symbols = rom_path.with_name(rom_path.name + ".ram.nl")
    prg_symbols.write_text(
        "\n".join(
            [
                "$8000#reset#Generated smoke ROM reset entry",
                "$8019#loop_inc#Main loop counter increment",
                "$801b#call_smoke_subroutine#Call deterministic smoke subroutine",
                "$802a#read_joy#Read joypad 1 through $4016",
                "$8043#loop_jump#Jump back to loop",
                "$8046#smoke_subroutine#Increment RAM subroutine used by step-over/out smoke",
                "",
            ]
        ),
        encoding="utf-8",
    )
    ram_symbols.write_text(
        "\n".join(
            [
                "$0000#frame_counter#Incremented each loop iteration",
                "$0001#joy_snapshot#Last sampled joypad mask",
                "$0002#a_counter#Incremented when A is observed",
                "$0003#right_counter#Incremented when Right is observed",
                "$0004#subroutine_counter#Incremented by the smoke subroutine",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return {"prg": prg_symbols, "ram": ram_symbols}


def wait_for_token_file(process, timeout):
    deadline = time.monotonic() + timeout
    lines = []
    selector = selectors.DefaultSelector()
    if process.stdout is not None:
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    if process.stderr is not None:
        selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            for stream in (process.stdout, process.stderr):
                if stream is not None:
                    remainder = stream.read()
                    if remainder:
                        lines.append(_text(remainder))
            remainder = _text(process.stderr.read()) if process.stderr else ""
            raise FceuxBridgeError(
                f"FCEUX exited with code {process.returncode} before bridge token file was printed:\n"
                + "".join(lines)
                + remainder
            )
        remaining = max(0.0, deadline - time.monotonic())
        events = selector.select(min(0.1, remaining))
        if not events:
            continue
        for key, _ in events:
            line = key.fileobj.readline()
            if not line:
                continue
            text = _text(line)
            lines.append(text)
            match = TOKEN_RE.search(text)
            if match:
                return match.group("path").strip()
    raise FceuxBridgeError(
        f"timed out after {timeout:.1f}s waiting for bridge token file; command was still running:\n"
        + "".join(lines)
    )


def launch_fceux(
    fceux,
    rom,
    *,
    bridge="tcp:127.0.0.1:0",
    timeout=15.0,
    env=None,
    bridge_headless=False,
    bridge_arg_style="equals",
):
    if bridge_arg_style == "equals":
        command = [str(fceux), f"--bridge={bridge}"]
    elif bridge_arg_style == "split":
        command = [str(fceux), "--bridge", str(bridge)]
    else:
        raise FceuxBridgeError(f"unsupported bridge_arg_style: {bridge_arg_style}")
    if bridge_headless:
        command.append("--bridge-headless")
    if rom is not None:
        command.append(str(rom))
    process_env = os.environ.copy()
    if env:
        process_env.update(env)
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=process_env,
    )
    try:
        token_file = wait_for_token_file(process, timeout)
    except Exception:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
        raise
    return process, token_file


def read_process_stdout_line(process, timeout):
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    events = selector.select(timeout)
    if not events:
        raise FceuxBridgeError(f"timed out waiting for stdio bridge response after {timeout:.1f}s")
    line = process.stdout.readline()
    if not line:
        raise FceuxBridgeError("bridge closed before writing a stdio response")
    return _text(line)


def run_stdio_auth_failure_smoke(fceux, rom, *, timeout=15.0, env=None, bridge_headless=True):
    process, token_file = launch_fceux(
        fceux,
        rom,
        bridge="stdio",
        timeout=timeout,
        env=env,
        bridge_headless=bridge_headless,
        bridge_arg_style="split",
    )
    try:
        endpoint, _ = _read_token(token_file)
        if endpoint != "stdio":
            raise FceuxBridgeError(f"expected stdio token endpoint, got {endpoint}")
        process.stdin.write(b"HELLO definitely-wrong-token\n")
        process.stdin.flush()
        response = json.loads(read_process_stdout_line(process, timeout))
        if response.get("ok") is not False or response.get("error") != "auth required":
            raise FceuxBridgeError(f"bad token did not return auth failure: {response!r}")
        return response
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


def _read_token(path):
    with open(path, "r", encoding="utf-8") as f:
        endpoint = f.readline().strip()
        token = f.readline().strip()
    if not endpoint or not token:
        raise FceuxBridgeError(f"invalid token file: {path}")
    return endpoint, token


def run_smoke_bridge(f, *, rom=None, frames=2, screenshot=None, state_file=None):
    results = {}
    results["ping"] = f.ping()
    results["invalid_frame"] = f.command_raw("FRAME 0")
    results["parser_edges"] = {
        "unterminated_quote": f.command_raw('PING "unterminated'),
        "dangling_escape": f.command_raw("PING \\"),
        "hex_pc": f.reg_set("pc", "0x8019"),
        "dollar_pc": f.reg_set("pc", "$8019"),
        "decimal_a": f.reg_set("a", 32),
        "quoted_state_slot": f.state_save(slot="parser edge slot", inline=False),
        "reg_range_error": f.command_raw("REG_SET A 256"),
        "reg_unknown_error": f.command_raw("REG_SET SP 0"),
    }
    if rom is not None:
        results["load_rom"] = f.load_rom(str(rom))
    results["status_before"] = f.status()
    results["wait_idle"] = f.wait()
    results["trace_stop"] = f.trace_stop()
    results["trace_status_stopped"] = f.trace_status()
    results["trace_start"] = f.trace_start()
    results["trace_status_started"] = f.trace_status()
    results["history_config"] = f.history_config(size=512, enabled=True)
    results["frame"] = f.frame(frames)
    results["regs"] = f.regs()
    ram = f.memdump(0x0000, 16)
    results["ram0"] = ram["bytes"][0]
    saved = f.state_save(slot="smoke", inline=True)
    f.poke(0x0000, 0xA5)
    changed = f.peek(0x0000)
    f.state_load(slot=None, data=saved["bytes_data"])
    restored = f.peek(0x0000)
    results["state_roundtrip"] = {
        "changed": changed,
        "restored": restored,
        "inline_bytes": saved["bytes"],
    }
    if state_file:
        state_path = Path(state_file)
        file_saved = f.state_save(slot=None, path=str(state_path))
        f.poke(0x0000, 0x5A)
        file_changed = f.peek(0x0000)
        f.state_load(slot=None, path=str(state_path))
        file_restored = f.peek(0x0000)
        results["state_file_roundtrip"] = {
            "save": file_saved,
            "changed": file_changed,
            "restored": file_restored,
            "path": str(state_path),
            "exists": state_path.exists(),
            "size": state_path.stat().st_size if state_path.exists() else 0,
        }
    f.memload(0x0010, [0xDE, 0xAD, 0xBE, 0xEF])
    results["memsearch"] = {
        "cpu": f.memsearch([0xDE, 0xAD, None, 0xEF], domain="cpu", start=0x0010, end=0x001F, limit=4),
        "rom": f.memsearch([0x78, 0xD8, 0xA2, None, 0x9A], domain="rom", start=0, end=0x3FFF, limit=4),
        "ppu": f.memsearch([0x00, 0x00, 0x00, 0x00], domain="ppu", start=0, end=0x001F, limit=4),
    }
    results["history"] = f.history(8, include_disasm=False)
    history_disabled = f.history_config(enabled=False)
    f.frame(1)
    history_while_disabled = f.history(4, include_disasm=False)
    history_reenabled = f.history_config(enabled=True)
    f.frame(1)
    history_after_reenable = f.history(4, include_disasm=False)
    results["history_toggle"] = {
        "disabled": history_disabled,
        "while_disabled": history_while_disabled,
        "reenabled": history_reenabled,
        "after_reenable": history_after_reenable,
    }
    screen = f.rawscreen(inline=False)
    results["rawscreen"] = {
        key: screen.get(key)
        for key in ("width", "height", "visible_width", "visible_height", "format", "path")
        if key in screen
    }
    cart = f.cart_info()
    bank_8000 = f.bank_info("$8000")
    memmap = f.memmap()
    prg_peek = f.rom_peek(0, 16, domain="prg")
    chr_peek = f.rom_peek(0, 16, domain="chr")
    prg_dump = f.rom_dump("prg")
    chr_dump = f.rom_dump("chr")
    results["cart_rom"] = {
        "cart": {
            "ok": cart.get("ok"),
            "type": cart.get("type"),
            "mapper": cart.get("mapper"),
            "prg0_size": cart.get("prg", [{}])[0].get("size") if cart.get("prg") else None,
            "chr0_size": cart.get("chr", [{}])[0].get("size") if cart.get("chr") else None,
        },
        "bank_8000": {
            "ok": bank_8000.get("ok"),
            "address": bank_8000.get("address"),
            "start": bank_8000.get("mapping", {}).get("start"),
            "prg_offset": bank_8000.get("mapping", {}).get("prg_offset"),
        },
        "memmap": {
            "ok": memmap.get("ok"),
            "cpu_rows": len(memmap.get("cpu", [])),
            "ppu_rows": len(memmap.get("ppu", [])),
            "cpu_8000": next((row for row in memmap.get("cpu", []) if row.get("start") == "$8000"), None),
            "ppu_0000": next((row for row in memmap.get("ppu", []) if row.get("start") == "$0000"), None),
        },
        "prg_peek": {
            "ok": prg_peek.get("ok"),
            "domain": prg_peek.get("domain"),
            "offset": prg_peek.get("offset"),
            "len": prg_peek.get("len"),
            "size": prg_peek.get("size"),
            "bytes": list(prg_peek["bytes"]),
        },
        "chr_peek": {
            "ok": chr_peek.get("ok"),
            "domain": chr_peek.get("domain"),
            "offset": chr_peek.get("offset"),
            "len": chr_peek.get("len"),
            "size": chr_peek.get("size"),
            "bytes": list(chr_peek["bytes"]),
        },
        "prg_dump": {
            "ok": prg_dump.get("ok"),
            "domain": prg_dump.get("domain"),
            "len": prg_dump.get("len"),
            "size": prg_dump.get("size"),
            "bytes": len(prg_dump["bytes"]),
            "prefix": list(prg_dump["bytes"][:8]),
        },
        "chr_dump": {
            "ok": chr_dump.get("ok"),
            "domain": chr_dump.get("domain"),
            "len": chr_dump.get("len"),
            "size": chr_dump.get("size"),
            "bytes": len(chr_dump["bytes"]),
            "prefix": list(chr_dump["bytes"][:8]),
        },
    }
    cdlog_start = f.cdlog_start()
    f.frame(1)
    cdlog_dump = f.cdlog_dump(domain="all")
    cdlog_stop = f.cdlog_stop()
    results["cdlog"] = {
        "start": cdlog_start,
        "dump": {
            "ok": cdlog_dump.get("ok"),
            "domain": cdlog_dump.get("domain"),
            "status": cdlog_dump.get("status"),
            "cpu": {
                "size": cdlog_dump.get("cpu", {}).get("size"),
                "bytes": len(cdlog_dump.get("cpu", {}).get("bytes", b"")),
            } if cdlog_dump.get("cpu") is not None else None,
            "ppu": {
                "size": cdlog_dump.get("ppu", {}).get("size"),
                "bytes": len(cdlog_dump.get("ppu", {}).get("bytes", b"")),
            } if cdlog_dump.get("ppu") is not None else None,
        },
        "stop": cdlog_stop,
    }
    ppu_nametable = f.ppu_dump(domain="nametable", length=0x400)
    oam = f.oam_dump()
    palette = f.palette_dump()
    results["deep_state"] = {
        "ppu_nametable": {
            "ok": ppu_nametable.get("ok"),
            "domain": ppu_nametable.get("domain"),
            "len": ppu_nametable.get("len"),
            "bytes": len(ppu_nametable["bytes"]),
        },
        "oam": {
            "ok": oam.get("ok"),
            "len": oam.get("len"),
            "bytes": len(oam["bytes"]),
        },
        "palette": {
            "ok": palette.get("ok"),
            "palram_bytes": len(palette["palram_bytes"]),
            "upalram_bytes": len(palette["upalram_bytes"]),
        },
    }
    if screenshot:
        results["screenshot"] = f.screenshot(path=str(screenshot), inline=False)
        screenshot_path = Path(screenshot)
        results["screenshot_file"] = {
            "path": str(screenshot_path),
            "exists": screenshot_path.exists(),
            "size": screenshot_path.stat().st_size if screenshot_path.exists() else 0,
            "png": screenshot_path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n") if screenshot_path.exists() else False,
        }
    return results


def run_smoke(token_file, *, rom=None, frames=2, screenshot=None, state_file=None):
    with FceuxBridge.from_token_file(token_file) as f:
        return run_smoke_bridge(f, rom=rom, frames=frames, screenshot=screenshot, state_file=state_file)


def run_debugger_lua_smoke_bridge(f):
    results = {}
    f.pause()
    f.bp_clear_all()
    f.history_config(size=512, enabled=True)
    f.history_clear()
    results["run_until_timeout"] = f.command_raw("RUN_UNTIL break timeout_frames=1")
    bp = f.bp_set("$8019", mode="x", name="smoke-loop-inc")
    results["breakpoint"] = bp
    run = f.run_until_break(timeout_frames=30)
    results["run_until_break"] = run
    results["break_status"] = f.break_status()
    results["history_at_break"] = f.history(128, include_disasm=True)
    f.bp_clear(bp["id"])
    results["reg_set_loop"] = f.reg_set("pc", "$8019")
    results["step"] = f.step(timeout_frames=30)
    results["regs_after_step"] = f.regs()
    results["reg_set_call"] = f.reg_set("pc", "$801b")
    results["step_over"] = f.step_over(timeout_frames=60)
    results["reg_set_stack"] = f.reg_set("s", "$fb")
    f.poke("$01fc", 0x1D)
    f.poke("$01fd", 0x80)
    results["reg_set_subroutine"] = f.reg_set("pc", "$8046")
    results["step_out"] = f.step_out(timeout_frames=60)
    results["disasm"] = f.disasm("$8019", 2)
    f.history_clear()
    watch = f.watch_set("$0000", mode="w", name="smoke-write-zero")
    results["watchpoint"] = watch
    watch_run = f.run_until_break(timeout_frames=60)
    results["watch_run_until_break"] = watch_run
    results["history_at_watch"] = f.history(128, include_disasm=True)
    f.bp_clear(watch["id"])
    lua_first = f.lua_eval("bridge_smoke_counter = (bridge_smoke_counter or 0) + 1; print('lua-smoke'); return bridge_smoke_counter")
    lua_error = f.command_raw("LUA_EVAL \"print('before-error'); error('bridge smoke error')\"")
    lua_yield_error = f.command_raw("LUA_EVAL \"print('before-yield'); emu.frameadvance(); print('after-yield')\"")
    lua_second = f.lua_eval("bridge_smoke_counter = bridge_smoke_counter + 1; return bridge_smoke_counter")
    results["lua_first"] = lua_first
    results["lua_error"] = lua_error
    results["lua_yield_error"] = lua_yield_error
    results["lua_second"] = lua_second
    return results


def run_debugger_lua_smoke(token_file):
    with FceuxBridge.from_token_file(token_file) as f:
        return run_debugger_lua_smoke_bridge(f)


def run_input_smoke_bridge(f):
    results = {}
    f.pause()
    f.joy_clear()
    before = f.memdump(0x0000, 4)["bytes"]
    results["before"] = list(before)
    f.press(0, "a", frames=2)
    after_a = f.memdump(0x0000, 4)["bytes"]
    results["after_a"] = list(after_a)
    with f.hold(0, "right"):
        held_state = f.input_state()
        f.frame(2)
    after_right = f.memdump(0x0000, 4)["bytes"]
    results["held_state"] = held_state
    results["after_right"] = list(after_right)
    results["cleared_state"] = f.input_state()
    return results


def run_input_smoke(token_file):
    with FceuxBridge.from_token_file(token_file) as f:
        return run_input_smoke_bridge(f)


def run_symbol_smoke_bridge(f, rom):
    if rom is None:
        raise FceuxBridgeError("symbol smoke requires a generated or supplied ROM path")
    symbols = write_symbol_files(rom)
    results = {}
    results["load_prg"] = f.sym_load(symbols["prg"], bank=0)
    results["resolve_loop"] = f.sym_resolve("loop_inc", bank=0)
    results["lookup_loop"] = f.sym_lookup("$8019", bank=0)
    results["load_ram"] = f.sym_load(symbols["ram"], bank=-1)
    results["resolve_a_counter"] = f.sym_resolve("a_counter", bank=-1)
    results["lookup_a_counter"] = f.sym_lookup("$0002", bank=-1)
    return results


def run_symbol_smoke(token_file, rom):
    with FceuxBridge.from_token_file(token_file) as f:
        return run_symbol_smoke_bridge(f, rom)


def run_app_exit_smoke_bridge(f, process, *, timeout=5.0):
    response = f.app_exit()
    if response.get("exiting") is not True:
        raise FceuxBridgeError(f"APP_EXIT did not report exiting=true: {response!r}")
    try:
        returncode = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise FceuxBridgeError(f"APP_EXIT did not terminate FCEUX within {timeout:.1f}s") from exc
    return {
        "response": response,
        "exited": True,
        "returncode": returncode,
    }


def assert_smoke_results(results):
    if not results.get("ping", {}).get("pong"):
        raise FceuxBridgeError("PING did not return pong=true")
    invalid_frame = results.get("invalid_frame", {})
    if invalid_frame.get("ok") is not False or "positive integer" not in invalid_frame.get("error", ""):
        raise FceuxBridgeError("FRAME 0 did not return the documented structured error")
    parser_edges = results.get("parser_edges", {})
    unterminated = parser_edges.get("unterminated_quote", {})
    dangling = parser_edges.get("dangling_escape", {})
    if unterminated.get("ok") is not False or "unterminated quote" not in unterminated.get("error", ""):
        raise FceuxBridgeError("command tokenizer did not report unterminated quote")
    if dangling.get("ok") is not False or "dangling escape" not in dangling.get("error", ""):
        raise FceuxBridgeError("command tokenizer did not report dangling escape")
    if parser_edges.get("hex_pc", {}).get("PC") != "$8019" or parser_edges.get("dollar_pc", {}).get("PC") != "$8019":
        raise FceuxBridgeError("REG_SET did not accept 0x and $ numeric forms")
    if parser_edges.get("decimal_a", {}).get("A") != "$20":
        raise FceuxBridgeError("REG_SET did not accept decimal numeric form")
    quoted_slot = parser_edges.get("quoted_state_slot", {})
    if not quoted_slot.get("ok") or quoted_slot.get("slot") != "parser edge slot":
        raise FceuxBridgeError("quoted option token did not round-trip through STATE_SAVE slot")
    reg_range_error = parser_edges.get("reg_range_error", {})
    if reg_range_error.get("ok") is not False or "out of range" not in reg_range_error.get("error", ""):
        raise FceuxBridgeError("REG_SET A 256 did not return a range error")
    reg_unknown_error = parser_edges.get("reg_unknown_error", {})
    if reg_unknown_error.get("ok") is not False or "unsupported register" not in reg_unknown_error.get("error", ""):
        raise FceuxBridgeError("REG_SET unsupported register did not return a structured error")
    if not results.get("status_before", {}).get("game_loaded"):
        raise FceuxBridgeError("STATUS did not report a loaded game")
    wait_idle = results.get("wait_idle", {})
    if not wait_idle.get("ok") or wait_idle.get("gate_active") is not False:
        raise FceuxBridgeError("WAIT did not return an idle gate status")
    trace_stop = results.get("trace_stop", {})
    trace_status_stopped = results.get("trace_status_stopped", {})
    if trace_stop.get("enabled") is not False or trace_stop.get("registered") is not False:
        raise FceuxBridgeError("TRACE_STOP did not disable history capture")
    if trace_status_stopped.get("enabled") is not False or trace_status_stopped.get("registered") is not False:
        raise FceuxBridgeError("TRACE_STATUS did not report stopped capture after TRACE_STOP")
    trace_start = results.get("trace_start", {})
    trace_status_started = results.get("trace_status_started", {})
    if trace_start.get("enabled") is not True or trace_start.get("registered") is not True:
        raise FceuxBridgeError("TRACE_START did not enable history capture")
    if trace_status_started.get("enabled") is not True or trace_status_started.get("registered") is not True:
        raise FceuxBridgeError("TRACE_STATUS did not report enabled capture after TRACE_START")
    regs = results.get("regs", {})
    if "PC" not in regs or "frame" not in regs:
        raise FceuxBridgeError("REGS response is missing PC or frame")
    if not isinstance(results.get("ram0"), int):
        raise FceuxBridgeError("MEMDUMP did not return decoded RAM bytes")
    roundtrip = results.get("state_roundtrip", {})
    if roundtrip.get("changed", {}).get("value") != 0xA5:
        raise FceuxBridgeError("POKE/PEEK verification failed")
    if roundtrip.get("restored", {}).get("value") == 0xA5:
        raise FceuxBridgeError("STATE_LOAD did not restore RAM after inline save")
    file_roundtrip = results.get("state_file_roundtrip")
    if file_roundtrip is not None:
        if not file_roundtrip.get("save", {}).get("ok"):
            raise FceuxBridgeError("STATE_SAVE path did not return ok=true")
        if not file_roundtrip.get("exists") or file_roundtrip.get("size", 0) <= 0:
            raise FceuxBridgeError("STATE_SAVE path did not write a state file")
        if file_roundtrip.get("changed", {}).get("value") != 0x5A:
            raise FceuxBridgeError("POKE/PEEK verification failed before state file restore")
        if file_roundtrip.get("restored", {}).get("value") == 0x5A:
            raise FceuxBridgeError("STATE_LOAD path did not restore RAM after file save")
    if not results.get("history", {}).get("entries"):
        raise FceuxBridgeError("HISTORY returned no entries after frame execution")
    history_toggle = results.get("history_toggle", {})
    disabled = history_toggle.get("disabled", {})
    while_disabled = history_toggle.get("while_disabled", {})
    reenabled = history_toggle.get("reenabled", {})
    after_reenable = history_toggle.get("after_reenable", {})
    if disabled.get("enabled") is not False or disabled.get("registered") is not False:
        raise FceuxBridgeError("HISTORY_CONFIG enabled=false did not unregister history capture")
    if while_disabled.get("enabled") is not False or while_disabled.get("entries"):
        raise FceuxBridgeError("HISTORY recorded entries while disabled")
    if reenabled.get("enabled") is not True or reenabled.get("registered") is not True:
        raise FceuxBridgeError("HISTORY_CONFIG enabled=true did not re-register history capture")
    if not after_reenable.get("entries"):
        raise FceuxBridgeError("HISTORY did not record entries after being re-enabled")
    memsearch = results.get("memsearch", {})
    cpu_search = memsearch.get("cpu", {})
    if not cpu_search.get("ok") or cpu_search.get("domain") != "cpu" or "$0010" not in cpu_search.get("matches", []):
        raise FceuxBridgeError("MEMSEARCH cpu did not find the injected RAM wildcard pattern")
    rom_search = memsearch.get("rom", {})
    if not rom_search.get("ok") or rom_search.get("domain") != "rom" or "$000000" not in rom_search.get("matches", []):
        raise FceuxBridgeError("MEMSEARCH rom did not find the generated PRG reset program")
    ppu_search = memsearch.get("ppu", {})
    if not ppu_search.get("ok") or ppu_search.get("domain") != "ppu" or "$0000" not in ppu_search.get("matches", []):
        raise FceuxBridgeError("MEMSEARCH ppu did not find the generated zero CHR pattern")
    if ppu_search.get("truncated") is not True:
        raise FceuxBridgeError("MEMSEARCH ppu limit did not report truncation for repeated zero pattern")
    screen = results.get("rawscreen", {})
    if screen.get("width") != 256 or screen.get("height") != 240 or screen.get("format") != "rgba8888":
        raise FceuxBridgeError("RAWSCREEN metadata is not the expected 256x240 rgba8888 shape")
    cart_rom = results.get("cart_rom", {})
    cart = cart_rom.get("cart", {})
    if not cart.get("ok") or cart.get("type") != "cart" or cart.get("mapper") != 0:
        raise FceuxBridgeError("CART_INFO did not report the generated NROM cartridge")
    if cart.get("prg0_size") != 0x4000 or cart.get("chr0_size") != 0x2000:
        raise FceuxBridgeError("CART_INFO did not report the generated ROM PRG/CHR sizes")
    bank_8000 = cart_rom.get("bank_8000", {})
    if not bank_8000.get("ok") or bank_8000.get("address") != "$8000" or bank_8000.get("start") != "$8000" or bank_8000.get("prg_offset") != "$000000":
        raise FceuxBridgeError("BANK_INFO $8000 did not report the expected PRG mapping")
    memmap = cart_rom.get("memmap", {})
    cpu_8000 = memmap.get("cpu_8000") or {}
    ppu_0000 = memmap.get("ppu_0000") or {}
    if not memmap.get("ok") or memmap.get("cpu_rows", 0) < 8 or memmap.get("ppu_rows", 0) < 8:
        raise FceuxBridgeError("MEMMAP did not return the expected CPU/PPU mapping rows")
    if cpu_8000.get("prg_offset") != "$000000" or ppu_0000.get("chr_offset") != "$000000":
        raise FceuxBridgeError("MEMMAP did not expose the expected PRG/CHR offsets")
    prg_peek = cart_rom.get("prg_peek", {})
    expected_prg_prefix = [0x78, 0xD8, 0xA2, 0xFF, 0x9A, 0xA9, 0x00, 0x85]
    if not prg_peek.get("ok") or prg_peek.get("domain") != "prg" or prg_peek.get("offset") != "$000000" or prg_peek.get("len") != 16:
        raise FceuxBridgeError("ROM_PEEK PRG did not return the expected response metadata")
    if prg_peek.get("size") != 0x4000 or prg_peek.get("bytes", [])[: len(expected_prg_prefix)] != expected_prg_prefix:
        raise FceuxBridgeError("ROM_PEEK PRG did not return the generated reset program bytes")
    chr_peek = cart_rom.get("chr_peek", {})
    if not chr_peek.get("ok") or chr_peek.get("domain") != "chr" or chr_peek.get("size") != 0x2000 or chr_peek.get("bytes") != [0] * 16:
        raise FceuxBridgeError("ROM_PEEK CHR did not return the generated CHR bytes")
    prg_dump = cart_rom.get("prg_dump", {})
    if not prg_dump.get("ok") or prg_dump.get("domain") != "prg" or prg_dump.get("len") != 0x4000 or prg_dump.get("size") != 0x4000 or prg_dump.get("bytes") != 0x4000:
        raise FceuxBridgeError("ROM_DUMP PRG did not return the generated PRG size")
    if prg_dump.get("prefix") != expected_prg_prefix:
        raise FceuxBridgeError("ROM_DUMP PRG prefix did not match the generated reset program")
    chr_dump = cart_rom.get("chr_dump", {})
    if not chr_dump.get("ok") or chr_dump.get("domain") != "chr" or chr_dump.get("len") != 0x2000 or chr_dump.get("size") != 0x2000 or chr_dump.get("bytes") != 0x2000:
        raise FceuxBridgeError("ROM_DUMP CHR did not return the generated CHR size")
    if chr_dump.get("prefix") != [0] * 8:
        raise FceuxBridgeError("ROM_DUMP CHR prefix did not match the generated CHR bytes")
    cdlog = results.get("cdlog", {})
    cdlog_start = cdlog.get("start", {})
    if not cdlog_start.get("ok") or cdlog_start.get("enabled") is not True or cdlog_start.get("cpu_size") != 0x4000 or cdlog_start.get("ppu_size") != 0x2000:
        raise FceuxBridgeError("CDLOG_START did not enable logger buffers for the generated ROM")
    cdlog_dump = cdlog.get("dump", {})
    if not cdlog_dump.get("ok") or cdlog_dump.get("domain") != "all":
        raise FceuxBridgeError("CDLOG_DUMP all did not return ok domain metadata")
    if cdlog_dump.get("cpu", {}).get("bytes") != 0x4000 or cdlog_dump.get("ppu", {}).get("bytes") != 0x2000:
        raise FceuxBridgeError("CDLOG_DUMP all did not return CPU/PPU logger buffers")
    if cdlog_dump.get("status", {}).get("code", 0) <= 0:
        raise FceuxBridgeError("CDLOG_DUMP did not record executed PRG code after a frame")
    cdlog_stop = cdlog.get("stop", {})
    if not cdlog_stop.get("ok") or cdlog_stop.get("enabled") is not False:
        raise FceuxBridgeError("CDLOG_STOP did not disable code/data logging")
    deep_state = results.get("deep_state", {})
    ppu_nametable = deep_state.get("ppu_nametable", {})
    if not ppu_nametable.get("ok") or ppu_nametable.get("domain") != "nametable" or ppu_nametable.get("len") != 0x400 or ppu_nametable.get("bytes") != 0x400:
        raise FceuxBridgeError("PPU_DUMP nametable did not return the expected 0x400 direct bytes")
    oam = deep_state.get("oam", {})
    if not oam.get("ok") or oam.get("len") != 0x100 or oam.get("bytes") != 0x100:
        raise FceuxBridgeError("OAM_DUMP did not return the expected 256 bytes")
    palette = deep_state.get("palette", {})
    if not palette.get("ok") or palette.get("palram_bytes") != 0x20 or palette.get("upalram_bytes") != 0x03:
        raise FceuxBridgeError("PALETTE_DUMP did not return the expected palette RAM sizes")
    if "screenshot" in results:
        screenshot = results.get("screenshot", {})
        screenshot_file = results.get("screenshot_file", {})
        if not screenshot.get("ok") or screenshot.get("path") != screenshot_file.get("path"):
            raise FceuxBridgeError("SCREENSHOT path response did not report the expected path")
        if not screenshot_file.get("exists") or screenshot_file.get("size", 0) <= 8 or not screenshot_file.get("png"):
            raise FceuxBridgeError("SCREENSHOT path did not write a valid PNG file")


def _lua_number(response):
    result = response.get("result")
    if isinstance(result, list) and len(result) == 1:
        return result[0]
    return result


def assert_debugger_lua_smoke_results(results):
    timeout = results.get("run_until_timeout", {})
    if timeout.get("ok") is not False or timeout.get("timeout") is not True or timeout.get("error") != "timeout":
        raise FceuxBridgeError("RUN_UNTIL break timeout did not return a structured timeout error")
    bp = results.get("breakpoint", {}).get("breakpoint", {})
    if bp.get("addr") != "$8019" or bp.get("mode") != "x":
        raise FceuxBridgeError("BP_SET did not create the expected execute breakpoint at $8019")
    run_break = results.get("run_until_break", {}).get("break", {})
    if not results.get("run_until_break", {}).get("ok") or not run_break.get("hit"):
        raise FceuxBridgeError("RUN_UNTIL break did not report a breakpoint hit")
    if run_break.get("id") != bp.get("id"):
        raise FceuxBridgeError("RUN_UNTIL break hit did not match the created breakpoint id")
    history_entries = results.get("history_at_break", {}).get("entries", [])
    if not history_entries:
        raise FceuxBridgeError("HISTORY returned no entries at breakpoint")
    hit_pc = run_break.get("PC")
    if hit_pc and not any(entry.get("PC") == hit_pc for entry in history_entries):
        raise FceuxBridgeError("HISTORY at breakpoint does not include the reported breakpoint PC")
    step_break = results.get("step", {}).get("break", {})
    if not results.get("step", {}).get("ok") or not step_break.get("hit"):
        raise FceuxBridgeError("STEP did not report a debugger break")
    if results.get("reg_set_loop", {}).get("PC") != "$8019":
        raise FceuxBridgeError("REG_SET PC did not position the CPU at $8019")
    step_over_break = results.get("step_over", {}).get("break", {})
    if not results.get("step_over", {}).get("ok") or step_over_break.get("PC") != "$801e":
        raise FceuxBridgeError("STEP_OVER did not stop after the generated JSR at $801b")
    if results.get("reg_set_stack", {}).get("S") != "$fb" or results.get("reg_set_subroutine", {}).get("PC") != "$8046":
        raise FceuxBridgeError("REG_SET did not establish the generated subroutine/stack state")
    step_out_break = results.get("step_out", {}).get("break", {})
    if not results.get("step_out", {}).get("ok") or step_out_break.get("PC") != "$801e":
        raise FceuxBridgeError("STEP_OUT did not stop at the generated subroutine return site")
    watch = results.get("watchpoint", {}).get("breakpoint", {})
    if watch.get("addr") != "$0000" or watch.get("mode") != "w":
        raise FceuxBridgeError("WATCH_SET did not create the expected write watchpoint at $0000")
    watch_break = results.get("watch_run_until_break", {}).get("break", {})
    if not results.get("watch_run_until_break", {}).get("ok") or not watch_break.get("hit"):
        raise FceuxBridgeError("RUN_UNTIL break did not report a watchpoint hit")
    if watch_break.get("id") != watch.get("id") or watch_break.get("PC") != "$8019":
        raise FceuxBridgeError("RUN_UNTIL break watchpoint hit did not match the write to $0000")
    watch_history = results.get("history_at_watch", {}).get("entries", [])
    if not any(entry.get("PC") == "$8019" and entry.get("effective_addr") == "$0000" for entry in watch_history):
        raise FceuxBridgeError("HISTORY at watchpoint does not include the $0000 write")
    if _lua_number(results.get("lua_first", {})) != 1 or _lua_number(results.get("lua_second", {})) != 2:
        raise FceuxBridgeError("Lua eval state did not persist across calls")
    if "lua-smoke" not in results.get("lua_first", {}).get("output", ""):
        raise FceuxBridgeError("Lua eval did not capture print output")
    lua_error = results.get("lua_error", {})
    if lua_error.get("ok") is not False or "bridge smoke error" not in lua_error.get("error", ""):
        raise FceuxBridgeError("Lua eval error did not return a structured error")
    if "before-error" not in lua_error.get("output", ""):
        raise FceuxBridgeError("Lua eval error did not preserve captured output")
    lua_yield_error = results.get("lua_yield_error", {})
    if lua_yield_error.get("ok") is not False or "yield" not in lua_yield_error.get("error", ""):
        raise FceuxBridgeError("Lua eval frameadvance/yield did not return a structured error")
    if "before-yield" not in lua_yield_error.get("output", ""):
        raise FceuxBridgeError("Lua eval frameadvance/yield error did not preserve captured output")
    if "after-yield" in lua_yield_error.get("output", ""):
        raise FceuxBridgeError("Lua eval continued executing after frameadvance/yield error")


def assert_input_smoke_results(results):
    before = results.get("before", [])
    after_a = results.get("after_a", [])
    after_right = results.get("after_right", [])
    if len(before) < 4 or len(after_a) < 4 or len(after_right) < 4:
        raise FceuxBridgeError("input smoke memory snapshots are incomplete")
    if after_a[2] <= before[2]:
        raise FceuxBridgeError("press('a') did not increment generated ROM A counter at $0002")
    if after_right[3] <= after_a[3]:
        raise FceuxBridgeError("hold('right') did not increment generated ROM Right counter at $0003")
    joypads = results.get("cleared_state", {}).get("joypads", [])
    if joypads and any(joypads[0].values()):
        raise FceuxBridgeError("bridge input did not clear after press/hold helpers")


def _symbol_name(response):
    symbol = response.get("symbol")
    if not symbol:
        return None
    return symbol.get("name")


def assert_symbol_smoke_results(results):
    if results.get("load_prg", {}).get("format") != "fceux-nl":
        raise FceuxBridgeError("explicit PRG .nl file did not load as fceux-nl")
    if _symbol_name(results.get("resolve_loop", {})) != "loop_inc":
        raise FceuxBridgeError("SYM_RESOLVE did not find loop_inc in PRG .nl symbols")
    if _symbol_name(results.get("lookup_loop", {})) != "loop_inc":
        raise FceuxBridgeError("SYM_LOOKUP did not find loop_inc at $8019")
    if results.get("load_ram", {}).get("bank") != -1:
        raise FceuxBridgeError("explicit RAM .nl file did not load as bank -1")
    if _symbol_name(results.get("resolve_a_counter", {})) != "a_counter":
        raise FceuxBridgeError("SYM_RESOLVE did not find a_counter in RAM .nl symbols")
    if _symbol_name(results.get("lookup_a_counter", {})) != "a_counter":
        raise FceuxBridgeError("SYM_LOOKUP did not find a_counter at $0002")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Run a FCEUX bridge runtime smoke workflow.")
    parser.add_argument("--fceux", help="FCEUX executable to launch. If omitted, --token-file is required.")
    parser.add_argument("--token-file", help="Existing bridge token file.")
    parser.add_argument("--rom", help="ROM to load. If omitted with --fceux, a tiny counter ROM is generated.")
    parser.add_argument("--frames", type=int, default=2)
    parser.add_argument("--screenshot", help="Optional PNG path for SCREENSHOT path=...")
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--mode", choices=("basic", "debugger-lua", "input", "symbols", "all"), default="basic")
    parser.add_argument("--bridge", default="tcp:127.0.0.1:0", help="Bridge endpoint for launched FCEUX.")
    parser.add_argument(
        "--qt-offscreen",
        action="store_true",
        help="Set QT_QPA_PLATFORM=offscreen for launched FCEUX.",
    )
    parser.add_argument(
        "--sdl-dummy",
        action="store_true",
        help="Set SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy for launched FCEUX.",
    )
    parser.add_argument(
        "--bridge-headless",
        action="store_true",
        help="Launch FCEUX in bridge-only mode without constructing the Qt console window.",
    )
    args = parser.parse_args(argv)

    if not args.fceux and not args.token_file:
        parser.error("either --fceux or --token-file is required")

    process = None
    with tempfile.TemporaryDirectory(prefix="fceux-bridge-smoke-") as temp_dir:
        temp_dir = Path(temp_dir)
        spaced_dir = temp_dir / "paths with spaces"
        spaced_dir.mkdir()
        rom = Path(args.rom) if args.rom else None
        if args.fceux and rom is None:
            rom = write_counter_rom(spaced_dir / "counter test.nes")
        screenshot_path = Path(args.screenshot) if args.screenshot else None
        if args.fceux and screenshot_path is None and args.mode in ("basic", "all"):
            screenshot_path = spaced_dir / "smoke screenshot.png"
        state_file_path = spaced_dir / "smoke state.fcs" if args.fceux and args.mode in ("basic", "all") else None

        token_file = args.token_file
        if args.fceux:
            env = {}
            if args.qt_offscreen or (args.bridge_headless and "QT_QPA_PLATFORM" not in os.environ):
                env["QT_QPA_PLATFORM"] = "offscreen"
            if args.sdl_dummy:
                env["SDL_VIDEODRIVER"] = "dummy"
                env["SDL_AUDIODRIVER"] = "dummy"
            if not env:
                env = None
            process, token_file = launch_fceux(
                args.fceux,
                rom,
                bridge=args.bridge,
                timeout=args.timeout,
                env=env,
                bridge_headless=args.bridge_headless,
            )

        try:
            if args.fceux and args.bridge.lower() == "stdio":
                endpoint, token = _read_token(token_file)
                if endpoint != "stdio":
                    raise FceuxBridgeError(f"expected stdio token endpoint, got {endpoint}")
                with FceuxBridge.from_process_stdio(process, token, timeout=args.timeout) as f:
                    f.pause()
                    results = {}
                    if args.mode in ("basic", "all"):
                        results["auth_failure"] = run_stdio_auth_failure_smoke(
                            args.fceux,
                            rom,
                            timeout=args.timeout,
                            env=env,
                            bridge_headless=args.bridge_headless,
                        )
                    if args.mode in ("basic", "all"):
                        results["basic"] = run_smoke_bridge(
                            f,
                            rom=None if args.fceux else rom,
                            frames=args.frames,
                            screenshot=screenshot_path,
                            state_file=state_file_path,
                        )
                        assert_smoke_results(results["basic"])
                    if args.mode in ("debugger-lua", "all"):
                        results["debugger_lua"] = run_debugger_lua_smoke_bridge(f)
                        assert_debugger_lua_smoke_results(results["debugger_lua"])
                    if args.mode in ("input", "all"):
                        results["input"] = run_input_smoke_bridge(f)
                        assert_input_smoke_results(results["input"])
                    if args.mode in ("symbols", "all"):
                        results["symbols"] = run_symbol_smoke_bridge(f, rom)
                        assert_symbol_smoke_results(results["symbols"])
                    results["app_exit"] = run_app_exit_smoke_bridge(f, process, timeout=args.timeout)
            else:
                if args.mode == "basic":
                    results = run_smoke(
                        token_file,
                        rom=None if args.fceux else rom,
                        frames=args.frames,
                        screenshot=screenshot_path,
                        state_file=state_file_path,
                    )
                    assert_smoke_results(results)
                elif args.mode == "debugger-lua":
                    results = run_debugger_lua_smoke(token_file)
                    assert_debugger_lua_smoke_results(results)
                elif args.mode == "input":
                    results = run_input_smoke(token_file)
                    assert_input_smoke_results(results)
                elif args.mode == "symbols":
                    results = run_symbol_smoke(token_file, rom)
                    assert_symbol_smoke_results(results)
                else:
                    results = {}
                    with FceuxBridge.from_token_file(token_file) as f:
                        results["basic"] = run_smoke_bridge(
                            f,
                            rom=None if args.fceux else rom,
                            frames=args.frames,
                            screenshot=screenshot_path,
                            state_file=state_file_path,
                        )
                        assert_smoke_results(results["basic"])
                        results["debugger_lua"] = run_debugger_lua_smoke_bridge(f)
                        assert_debugger_lua_smoke_results(results["debugger_lua"])
                        results["input"] = run_input_smoke_bridge(f)
                        assert_input_smoke_results(results["input"])
                        results["symbols"] = run_symbol_smoke_bridge(f, rom)
                        assert_symbol_smoke_results(results["symbols"])
            print(json.dumps(results, indent=2, sort_keys=True))
        finally:
            if process is not None:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()


if __name__ == "__main__":
    main(sys.argv[1:])
