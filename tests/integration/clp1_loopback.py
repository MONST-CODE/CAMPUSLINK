import importlib.util
import socket
import threading
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("clp1", Path(__file__).resolve().parents[2] / "tools/clp1_server.py")
clp1 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clp1)


class Protocol(unittest.TestCase):
    def client(self, exercise, expect_error=False):
        errors = []
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(3)

            def server():
                try:
                    peer, _ = listener.accept()
                    with peer:
                        clp1.respond(peer)
                except Exception as exc:
                    errors.append(exc)

            thread = threading.Thread(target=server)
            thread.start()
            try:
                with socket.create_connection(listener.getsockname(), timeout=2) as client:
                    exercise(client)
            finally:
                thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            self.assertEqual(bool(errors), expect_error, errors)
            if expect_error:
                self.assertIsInstance(errors[0], ValueError)

    def test_roundtrip(self):
        data = bytes(range(256)) * 4

        def exercise(client):
            client.sendall(b"CLP1 1024\n")
            with client.makefile("rb") as io:
                self.assertEqual(io.read(4), b"RDY1")
                client.sendall(data)
                self.assertEqual(io.read(4), b"ACK1")
                client.sendall(b"GO1\n")
                self.assertEqual(io.read(len(data)), data)

        self.client(exercise)

    def test_limit(self):
        def exercise(client):
            client.sendall(b"CLP1 65537\n")
            self.assertEqual(client.recv(16), b"")

        self.client(exercise, expect_error=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
