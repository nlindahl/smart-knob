"""Exercise the actual Rust CLI against the C firmware protocol over a PTY."""
import os
import pathlib
import pty
import subprocess
import tempfile
import tty

root = pathlib.Path(__file__).resolve().parents[1]
cli = root / "target/debug/knobctl"
master, slave = pty.openpty()
tty.setraw(slave)
port = os.ttyname(slave)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
emulator = subprocess.Popen([str(root / "build-host/device_emulator"), str(master)], pass_fds=[master], env=env)
def run(*args, ok=True):
    result = subprocess.run([str(cli), *args, "--port", port], text=True, capture_output=True)
    assert (result.returncode == 0) == ok, result.stdout + result.stderr
    return result.stdout
try:
    run("upload", str(root / "build-host/demo.skv"), "--save")
    run("select", "4")
    assert "view=4" in run("state")
    with tempfile.NamedTemporaryFile(suffix=".skv") as f:
        f.write(b"SKV1" + bytes(100)); f.flush()
        run("upload", f.name, ok=False)
    assert "view=4" in run("state"), "Rejected upload changed the active view"
    run("upload", str(root / "build-host/demo.skv"))
    assert "view=4" in run("state"), "Live upload jumped away from the current view"
    assert "Motor: enabled=0" in run("diag")
    watched = run("watch", "--seconds", "0.3")
    assert "view=4 position=0" in watched and "samples, position range 0..0" in watched
    print("Rust/C integration: USB framing, upload, save, select, state, rejected upload preserves active view, "
          "re-upload keeps the current view, diag, watch")
finally:
    emulator.terminate(); emulator.wait(); os.close(master); os.close(slave)
