"""Verify the packaged emulator can serve an authenticated Bridge ping."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable')
parser.add_argument('--gui', action='store_true')
parser.add_argument('--platform', choices=('xcb', 'wayland', 'windows'))
args = parser.parse_args()
executable = Path(args.executable).resolve()
sdk = executable.parent.parent / 'sdk/python'
if not sdk.is_dir():
    sdk = Path(__file__).resolve().parents[1] / 'src/bridge/sdk/python'
sys.path.insert(0, str(sdk))
from fceux_bridge import FceuxBridge
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
if args.gui:
    env['QT_QPA_PLATFORM'] = args.platform or ('windows' if os.name == 'nt' else 'xcb')
    env['SDL_VIDEODRIVER'] = 'wayland' if args.platform == 'wayland' else ('windows' if os.name == 'nt' else 'x11')
if os.name == 'nt':
    # Prevent installed Qt or development tools from masking missing DLLs.
    system_root = os.environ['SystemRoot']
    env['PATH'] = f'{system_root}/System32;{system_root}'
    env['QT_PLUGIN_PATH'] = str(executable.parent)
    env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(executable.parent / 'platforms')
command = [str(executable), '--bridge=tcp:127.0.0.1:0']
if not args.gui:
    command.insert(1, '--bridge-headless')
with tempfile.TemporaryFile(mode='w+b') as log:
    process = subprocess.Popen(
        command,
        stdout=log, stderr=log, env=env,
    )
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            log.seek(0)
            output = log.read().decode('utf-8', errors='replace')
            match = re.search(r'token-file: ([^\r\n]+)', output)
            if match:
                with FceuxBridge.from_token_file(match[1].strip(), timeout=5) as bridge:
                    response = bridge.ping()
                    if not response.get('ok'):
                        raise RuntimeError(f'Ping failed: {response}')
                    print('Authenticated Bridge ping passed')
                break
            if process.poll() is not None:
                raise RuntimeError(f'Emulator exited with {process.returncode}')
            time.sleep(0.1)
        else:
            raise TimeoutError('Bridge token file was not announced within 30 seconds')
    except Exception:
        log.seek(0)
        print(log.read().decode('utf-8', errors='replace'), file=sys.stderr)
        raise
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
