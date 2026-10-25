"""Refuse firmware builds with stale generated map artifacts."""
from pathlib import Path
import runpy
Import("env")
module = runpy.run_path(str(Path(env.subst("$PROJECT_DIR")) / 'scripts/generate_point_map.py'))
module['run'](check=True)
