"""Verify the packaged emulator can serve an authenticated Bridge ping."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/bridge/sdk/python'))
from fceux_bridge import FceuxBridge

env = dict(os.environ, QT_QPA_PLATFORM='offscreen', SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
with tempfile.TemporaryFile(mode='w+b') as log:
    process = subprocess.Popen(
        [str(Path(sys.argv[1]).resolve()), '--bridge-headless', '--bridge=tcp:127.0.0.1:0'],
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
