#!/usr/bin/env python3
"""CAMPUSLINK CLP1 benchmark endpoint. Run only on an authorized private lab host."""
import argparse
import ipaddress
import socket
import time


def read_exact(client, count, deadline):
    data = bytearray()
    while len(data) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("deadline")
        client.settimeout(remaining)
        part = client.recv(count - len(data))
        if not part:
            raise ValueError("short request")
        data.extend(part)
    return data


def respond(client):
    deadline = time.monotonic() + 2.5
    header = bytearray()
    for _ in range(15):
        ch = read_exact(client, 1, deadline)
        if ch == b"\n":
            break
        header.extend(ch)
    else:
        raise ValueError("missing terminator")
    if not header.startswith(b"CLP1 ") or not header[5:].isdigit():
        raise ValueError("invalid protocol")
    count = int(header[5:])
    if not 1024 <= count <= 65536:
        raise ValueError("byte limit")
    client.sendall(b"RDY1")
    payload = read_exact(client, count, deadline)
    client.sendall(b"ACK1")
    if read_exact(client, 4, deadline) != b"GO1\n":
        raise ValueError("invalid go marker")
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("deadline")
    client.settimeout(remaining)
    client.sendall(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", type=ipaddress.IPv4Address, default=ipaddress.IPv4Address("127.0.0.1"))
    parser.add_argument("--allow", type=ipaddress.IPv4Address, default=ipaddress.IPv4Address("127.0.0.1"))
    parser.add_argument("--port", type=int, default=8123)
    parser.add_argument("--seconds", type=int, default=120)
    args = parser.parse_args()
    if (args.bind.is_unspecified or args.bind.is_multicast or args.allow.is_unspecified or args.allow.is_multicast
            or not 1 <= args.port <= 65535 or not 1 <= args.seconds <= 600):
        parser.error("Specify individual IPv4 addresses, valid port, and 1–600 seconds")
    expiry = time.monotonic() + args.seconds
    next_request = 0.0
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind((str(args.bind), args.port))
        listener.listen(2)
        while time.monotonic() < expiry:
            listener.settimeout(max(0.01, expiry - time.monotonic()))
            try:
                client, peer = listener.accept()
            except TimeoutError:
                break
            with client:
                if peer[0] != str(args.allow) or time.monotonic() < next_request:
                    continue
                next_request = time.monotonic() + 1
                try:
                    respond(client)
                except (OSError, ValueError):
                    continue


if __name__ == "__main__":
    main()
