"""Refuse firmware builds with a stale config_explain_table.inc."""
from pathlib import Path
import runpy
Import("env")
module = runpy.run_path(str(Path(env.subst("$PROJECT_DIR")) / 'scripts/generate_config_explain.py'))
module['run'](check=True)
