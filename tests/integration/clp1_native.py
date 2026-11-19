"""Integration test: the production Winsock transfer routine against the included server logic."""
import importlib.util
import socket
import subprocess
import sys
import threading
from pathlib import Path

spec = importlib.util.spec_from_file_location("clp1", Path(__file__).resolve().parents[2] / "tools/clp1_server.py")
clp1 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clp1)


def run(exe, correct):
    failures = []
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(8)

        def serve():
            try:
                for _ in range(3 if correct else 1):
                    connection, _ = listener.accept()
                    with connection:
                        if correct:
                            clp1.respond(connection)
                        else:
                            connection.sendall(b"NOPE")
            except (OSError, ValueError) as exc:
                failures.append(exc)

        worker = threading.Thread(target=serve)
        worker.start()
        try:
            process = subprocess.run([str(exe), "--clp1-loopback", str(listener.getsockname()[1])],
                                     capture_output=True, text=True, timeout=10, check=False)
        finally:
            worker.join(10)
        print(process.stdout.strip(), process.stderr.strip())
        if worker.is_alive() or failures or process.returncode != (0 if correct else 1):
            raise RuntimeError(f"Native CLP1 contract failure: {process.returncode}, {failures}")


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve(strict=True)
    run(executable, True)
    run(executable, False)
    print("PASS: authorized fixed loopback transfers; invalid server reply rejected")
