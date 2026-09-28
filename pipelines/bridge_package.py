"""Add source SDK, examples, documentation, and provenance to a binary package."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
stage = Path(sys.argv[1])
ignore = shutil.ignore_patterns('__pycache__', '*.pyc', '*.pyo')
bridge = root / 'src/bridge'
for name in ('sdk', 'docs'):
    shutil.copytree(bridge / name, stage / name, ignore=ignore)
shutil.copy2(root / 'COPYING', stage / 'COPYING')
shutil.copy2(bridge / 'README.md', stage / 'README.md')
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
(stage / 'BUILD-INFO.txt').write_text(
    f'Commit: {commit}\nWorkflow run: {os.environ.get("GITHUB_RUN_ID", "local")}\n',
    encoding='utf-8',
)
