"""Copy ESP Web Tools' prebundled modules and dependency notices for offline hosting."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
modules = root / "web/node_modules"
output = root / "web/dist/esp-web-tools"
if output.exists():
    shutil.rmtree(output)
shutil.copytree(modules / "esp-web-tools/dist/web", output)
for pattern in ("LICENSE*", "LICENCE*", "COPYING*", "NOTICE*", "OFL*"):
    for source in modules.rglob(pattern):
        if source.is_file():
            target = output / "licenses" / source.relative_to(modules)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
print(f"Staged ESP Web Tools in {output}")
