import tempfile
import unittest
from pathlib import Path

from fceux_bridge import FceuxBridge, FceuxBridgeError


class FakePipe:
    def __init__(self, responses):
        self.responses = list(responses)
        self.writes = []
        self.closed = False

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass

    def readline(self):
        if not self.responses:
            return b'{"ok":true}\n'
        return self.responses.pop(0)

    def close(self):
        self.closed = True


class FceuxBridgeClientTest(unittest.TestCase):
    def test_quote_leaves_simple_tokens_unquoted(self):
        bridge = FceuxBridge("stdio", "token", file=FakePipe([]))

        self.assertEqual(bridge.quote("domain=cpu"), "domain=cpu")
        self.assertEqual(bridge.quote("$8000"), "$8000")
        self.assertEqual(bridge.quote(""), '""')

    def test_quote_escapes_spaces_backslashes_and_quotes(self):
        bridge = FceuxBridge("stdio", "token", file=FakePipe([]))

        self.assertEqual(bridge.quote("/tmp/path with spaces/game.nes"), '"/tmp/path with spaces/game.nes"')
        self.assertEqual(bridge.quote(r'C:\roms\game.nes'), r'"C:\\roms\\game.nes"')
        self.assertEqual(bridge.quote('label "main"'), r'"label \"main\""')

    def test_command_raw_writes_one_line_and_decodes_json(self):
        pipe = FakePipe([b'{"ok":true,"value":7}\n'])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        response = bridge.command_raw("PING")

        self.assertEqual(response, {"ok": True, "value": 7})
        self.assertEqual(pipe.writes, [b"PING\n"])

    def test_command_raises_structured_error(self):
        bridge = FceuxBridge("stdio", "token", file=FakePipe([b'{"ok":false,"error":"auth required"}\n']))

        with self.assertRaisesRegex(FceuxBridgeError, "auth required"):
            bridge.command("PING")

    def test_from_token_file_rejects_missing_token(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            token_file = Path(temp_dir) / "bridge.token"
            token_file.write_text("stdio\n", encoding="utf-8")

            with self.assertRaisesRegex(FceuxBridgeError, "invalid token file"):
                FceuxBridge.from_token_file(token_file)

    def test_close_sends_quit_and_invokes_callback(self):
        called = []
        pipe = FakePipe([b'{"ok":true}\n'])
        bridge = FceuxBridge("stdio", "token", file=pipe, close_callback=lambda: called.append(True))

        bridge.close()

        self.assertEqual(pipe.writes, [b"QUIT\n"])
        self.assertTrue(pipe.closed)
        self.assertEqual(called, [True])
        self.assertIsNone(bridge.file)

    def test_memsearch_formats_iterable_patterns_with_wildcards(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.memsearch([0xAD, None, "??", "?", 0x120], domain="rom", start="$8000", end="$80ff", limit=7)

        self.assertEqual(pipe.writes, [b"MEMSEARCH hex:ad??????20 domain=rom limit=7 start=$8000 end=$80ff\n"])

    def test_memsearch_formats_string_patterns(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.memsearch("ad ?? 20", domain="ppu")

        self.assertEqual(pipe.writes, [b"MEMSEARCH hex:ad??20 domain=ppu limit=256\n"])

    def test_reg_set_formats_register_and_value(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.reg_set("pc", "$801b")
        bridge.reg_set("a", 0x20)

        self.assertEqual(pipe.writes, [b"REG_SET PC $801b\n", b"REG_SET A 32\n"])

    def test_history_and_execution_helpers_format_commands(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.history(7, include_disasm=False)
        bridge.history_config(size=1024, enabled=True)
        bridge.frame(3, wait=False)
        bridge.run_until_break(timeout_frames=9)
        bridge.run_until_pc("$8123", timeout_frames=10)
        bridge.run_until_frame(42, timeout_frames=11)
        bridge.step(2, timeout_frames=12)
        bridge.step_over(timeout_frames=13)
        bridge.step_out(timeout_frames=14)
        bridge.disasm("$8000", 4)
        bridge.callstack(5)

        self.assertEqual(
            pipe.writes,
            [
                b"HISTORY 7 include_disasm=false\n",
                b"HISTORY_CONFIG size=1024 enabled=true\n",
                b"FRAME 3\n",
                b"RUN_UNTIL break timeout_frames=9\n",
                b"RUN_UNTIL pc=$8123 timeout_frames=10\n",
                b"RUN_UNTIL frame=42 timeout_frames=11\n",
                b"STEP 2 timeout_frames=12\n",
                b"STEP_OVER timeout_frames=13\n",
                b"STEP_OUT timeout_frames=14\n",
                b"DISASM $8000 4\n",
                b"CALLSTACK 5\n",
            ],
        )

    def test_debugger_helpers_quote_optional_fields(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.bp_set("$8000", end="$80ff", domain="cpu", mode="xw", condition="A == 1", name="main loop", enabled=False)
        bridge.watch_set("$0002", mode="w", domain="cpu", condition="value == 3", name="joy counter")
        bridge.bp_clear(17)
        bridge.bp_clear_all()
        bridge.break_status()

        self.assertEqual(
            pipe.writes,
            [
                b'BP_SET $8000 domain=cpu mode=xw end=$80ff condition="A == 1" name="main loop" enabled=false\n',
                b'WATCH_SET $0002 domain=cpu mode=w condition="value == 3" name="joy counter"\n',
                b"BP_CLEAR 17\n",
                b"BP_CLEAR_ALL\n",
                b"BREAK_STATUS\n",
            ],
        )

    def test_symbol_and_lua_helpers_quote_commands(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.sym_load(auto=True)
        bridge.sym_load("/tmp/symbol file.nl", bank=-1)
        bridge.sym_resolve("main loop", bank=0)
        bridge.sym_lookup("$8000", bank=0)
        bridge.lua_eval('print("hello bridge")')
        bridge.lua_load("/tmp/script file.lua")
        bridge.lua_reset()
        bridge.lua_status()

        self.assertEqual(
            pipe.writes,
            [
                b"SYM_LOAD auto=true\n",
                b'SYM_LOAD "/tmp/symbol file.nl" bank=-1\n',
                b'SYM_RESOLVE "main loop" bank=0\n',
                b"SYM_LOOKUP $8000 bank=0\n",
                b'LUA_EVAL "print(\\"hello bridge\\")"\n',
                b'LUA_LOAD "/tmp/script file.lua"\n',
                b"LUA_RESET\n",
                b"LUA_STATUS\n",
            ],
        )

    def test_sym_load_rejects_missing_source(self):
        bridge = FceuxBridge("stdio", "token", file=FakePipe([]))

        with self.assertRaisesRegex(FceuxBridgeError, "sym_load requires path"):
            bridge.sym_load()

    def test_state_load_rejects_ambiguous_sources(self):
        bridge = FceuxBridge("stdio", "token", file=FakePipe([]))

        with self.assertRaisesRegex(FceuxBridgeError, "exactly one source"):
            bridge.state_load(slot=None)
        with self.assertRaisesRegex(FceuxBridgeError, "exactly one source"):
            bridge.state_load(slot="slot", path="/tmp/state.fcs")

    def test_state_save_and_load_format_slot_path_inline_and_data(self):
        pipe = FakePipe([
            b'{"ok":true,"base64":"AQID"}\n',
            b'{"ok":true}\n',
            b'{"ok":true}\n',
        ])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        saved = bridge.state_save(slot="agent slot", path="/tmp/state file.fcs", inline=True)
        bridge.state_load(slot=None, path="/tmp/state file.fcs")
        bridge.state_load(slot=None, data=b"\x04\x05")

        self.assertEqual(saved["bytes_data"], b"\x01\x02\x03")
        self.assertEqual(
            pipe.writes,
            [
                b'STATE_SAVE slot="agent slot" path="/tmp/state file.fcs" inline=true\n',
                b'STATE_LOAD path="/tmp/state file.fcs"\n',
                b"STATE_LOAD data=base64:BAU=\n",
            ],
        )

    def test_memory_and_video_helpers_decode_base64_payloads(self):
        pipe = FakePipe([
            b'{"ok":true,"base64":"3q2+7w=="}\n',
            b'{"ok":true,"base64":"AQIDBA=="}\n',
            b'{"ok":true,"palram_base64":"AAE=","upalram_base64":"AgM="}\n',
            b'{"ok":true,"base64":"BAUG"}\n',
        ])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        memdump = bridge.memdump("$0000", 4)
        ppu_dump = bridge.ppu_dump(domain="nametable", length=4)
        palette = bridge.palette_dump()
        rawscreen = bridge.rawscreen(path="/tmp/screen dump.rgba", inline=True)

        self.assertEqual(memdump["bytes"], b"\xde\xad\xbe\xef")
        self.assertEqual(ppu_dump["bytes"], b"\x01\x02\x03\x04")
        self.assertEqual(palette["palram_bytes"], b"\x00\x01")
        self.assertEqual(palette["upalram_bytes"], b"\x02\x03")
        self.assertEqual(rawscreen["pixels"], b"\x04\x05\x06")
        self.assertEqual(
            pipe.writes,
            [
                b"MEMDUMP $0000 4\n",
                b"PPU_DUMP domain=nametable len=4\n",
                b"PALETTE_DUMP\n",
                b'RAWSCREEN overlay=false path="/tmp/screen dump.rgba" inline=true\n',
            ],
        )

    def test_deep_introspection_helpers_decode_payloads_and_format_commands(self):
        pipe = FakePipe([
            b'{"ok":true,"base64":"AQID"}\n',
            b'{"ok":true,"base64":"BAUG"}\n',
            b'{"ok":true,"base64":"BwgJ"}\n',
            b'{"ok":true,"cpu":{"base64":"Cgs="},"ppu":{"base64":"DA0="}}\n',
            b'{"ok":true,"base64":"Dg8="}\n',
        ])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bus = bridge.buspeek("$8000", 3)
        ppu = bridge.ppu_peek("$2000", 3)
        rom = bridge.rom_peek(0, 3, domain="chr")
        cdlog = bridge.cdlog_dump(domain="all")
        oam = bridge.oam_dump()

        self.assertEqual(bus["bytes"], b"\x01\x02\x03")
        self.assertEqual(ppu["bytes"], b"\x04\x05\x06")
        self.assertEqual(rom["bytes"], b"\x07\x08\x09")
        self.assertEqual(cdlog["cpu"]["bytes"], b"\x0a\x0b")
        self.assertEqual(cdlog["ppu"]["bytes"], b"\x0c\x0d")
        self.assertEqual(oam["bytes"], b"\x0e\x0f")
        self.assertEqual(
            pipe.writes,
            [
                b"BUSPEEK $8000 3\n",
                b"PPU_PEEK $2000 3\n",
                b"ROM_PEEK 0 3 domain=chr\n",
                b"CDLOG_DUMP domain=all\n",
                b"OAM_DUMP\n",
            ],
        )

    def test_press_clears_joypad_after_frame(self):
        pipe = FakePipe([
            b'{"ok":true}\n',
            b'{"ok":true}\n',
            b'{"ok":true}\n',
        ])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.press(0, "a", frames=2)

        self.assertEqual(pipe.writes, [b"JOY 0 a\n", b"FRAME 2\n", b"WAIT\n", b"JOY_CLEAR 0\n"])

    def test_hold_context_clears_joypad_on_exit(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        with bridge.hold(1, "right", start=True):
            bridge.frame(1)

        self.assertEqual(pipe.writes, [b"JOY 1 right start\n", b"FRAME 1\n", b"WAIT\n", b"JOY_CLEAR 1\n"])

    def test_app_exit_writes_command(self):
        pipe = FakePipe([])
        bridge = FceuxBridge("stdio", "token", file=pipe)

        bridge.app_exit()

        self.assertEqual(pipe.writes, [b"APP_EXIT\n"])


if __name__ == "__main__":
    unittest.main()
