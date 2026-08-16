#!/usr/bin/env python3
"""Publish Codex runtime status to the TC002 MQTT status topic.

This is a tiny MQTT 3.1.1 publisher with no third-party dependency. It is
intended for local testing before Home Assistant is introduced.
"""

import argparse
import json
import socket
import struct
import time


def encode_remaining_length(length):
    encoded = bytearray()
    while True:
        value = length % 128
        length //= 128
        if length:
            value |= 128
        encoded.append(value)
        if not length:
            return bytes(encoded)


def utf8(value):
    data = value.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def read_packet(sock):
    first = sock.recv(1)
    if not first:
        raise RuntimeError("connection closed")
    multiplier = 1
    remaining = 0
    while True:
        encoded = sock.recv(1)
        if not encoded:
            raise RuntimeError("connection closed while reading length")
        byte = encoded[0]
        remaining += (byte & 127) * multiplier
        if byte & 128 == 0:
            break
        multiplier *= 128
    body = bytearray()
    while len(body) < remaining:
        chunk = sock.recv(remaining - len(body))
        if not chunk:
            raise RuntimeError("connection closed while reading body")
        body.extend(chunk)
    return first[0], bytes(body)


def send_packet(sock, packet_type, body):
    sock.sendall(bytes([packet_type]) + encode_remaining_length(len(body)) + body)


def publish(args):
    client_id = args.client_id or "codex-status-publisher"
    connect_flags = 0x02
    payload = utf8("MQTT") + bytes([4, connect_flags]) + struct.pack("!H", 30)
    payload += utf8(client_id)

    message = {
        "status": args.status,
        "message": args.message,
        "source": "codex",
        "ts": int(time.time()),
    }
    body = utf8(args.topic) + json.dumps(message, ensure_ascii=False).encode("utf-8")

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        send_packet(sock, 0x10, payload)
        packet_type, connack = read_packet(sock)
        if packet_type != 0x20 or len(connack) < 2 or connack[1] != 0:
            raise RuntimeError(f"MQTT connect failed: {connack!r}")
        send_packet(sock, 0x30, body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("status", choices=[
        "off", "idle", "working", "attention", "blocked"
    ])
    parser.add_argument("--host", required=True, help="MQTT broker host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="tc002/codex/status")
    parser.add_argument("--client-id", default="")
    parser.add_argument("--message", default="")
    args = parser.parse_args()
    publish(args)


if __name__ == "__main__":
    main()
