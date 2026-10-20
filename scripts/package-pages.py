"""Stage the static editor and dependency notices for GitHub Pages."""
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
site = root / "build-pages"
if site.exists():
    shutil.rmtree(site)
site.mkdir()
for name in ("index.html", "bridge.js", "preview.js", "preview.wasm"):
    shutil.copy2(root / "web" / name, site / name)
shutil.copytree(root / "web/pkg", site / "pkg")
(site / ".nojekyll").touch()
for name in ("LICENSE.md", "THIRD_PARTY.md"):
    shutil.copy2(root / name, site / name)

# Retain notices for the actual WASM dependency graph, including font licenses.
metadata = json.loads(subprocess.check_output([
    "cargo", "metadata", "--offline", "--locked", "--format-version", "1",
    "--filter-platform", "wasm32-unknown-unknown",
], cwd=root))
packages = {p["id"]: p for p in metadata["packages"]}
nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
pending = [p["id"] for p in packages.values() if p["name"] == "knob-editor"]
seen = set()
notice_count = 0
while pending:
    package_id = pending.pop()
    if package_id in seen:
        continue
    seen.add(package_id)
    pending.extend(nodes[package_id]["dependencies"])
    package = packages[package_id]
    if package["source"] is None:
        continue  # Workspace code is covered by the project's LICENSE.md.
    folder = Path(package["manifest_path"]).parent
    notices = set()
    for pattern in ("LICENSE*", "COPYING*", "NOTICE*", "OFL*"):
        notices.update(p for p in folder.rglob(pattern) if p.is_file())
    if package.get("license_file"):
        notices.add(folder / package["license_file"])
    for source in notices:
        target = site / "licenses" / f"{package['name']}-{package['version']}" / source.relative_to(folder)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        notice_count += 1

lvgl = Path(os.environ.get("LVGL_DIR", root.parent / "modules/lib/gui/lvgl")).resolve()
for source in lvgl.glob("LICEN?E*"):
    if source.is_file():
        target = site / "licenses/lvgl" / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        notice_count += 1
if not (site / "pkg/knob_editor_bg.wasm").is_file():
    raise RuntimeError("Build the Rust editor before packaging Pages")
print(f"Staged {site} with {notice_count} dependency notice files")
