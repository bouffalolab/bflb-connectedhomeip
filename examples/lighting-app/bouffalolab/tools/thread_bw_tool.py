#!/usr/bin/env python3
"""Thread UDP bandwidth helper for Bouffalo lighting-app UDP test endpoint.

This script drives a UDP control/data endpoint hosted on a BL706 Matter lighting app.
The endpoint protocol is intentionally simple ASCII over UDP:

  BWT1 RESET\n
  BWT1 GET_STATS\n
  BWT1 START_TX payload=<N> duration_ms=<N> dest_port=<N> chunk_count=<N>\n
Responses:
  BWT1 OK ...\n
  BWT1 STATS key=value ...\n
  BWT1 ERR code=<N> reason=<text>\n
Data packets are raw UDP datagrams. The first 4 bytes are a big-endian sequence number.
"""

from __future__ import annotations

import argparse
import json
import select
import socket
import struct
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional

CONTROL_PREFIX = b"BWT1"
DEFAULT_PORT = 33333
DEFAULT_CONTROL_TIMEOUT_SEC = 2.0
DEFAULT_RX_GRACE_SEC = 1.0
SEQ_HEADER_LEN = 4


@dataclass
class TrialMetrics:
    bytes_sent: int = 0
    bytes_received: int = 0
    pkts_sent: int = 0
    pkts_received: int = 0
    first_seq: Optional[int] = None
    last_seq: Optional[int] = None
    seq_gap_count: int = 0
    start_monotonic: float = 0.0
    end_monotonic: float = 0.0

    @property
    def duration_sec(self) -> float:
        return max(0.0, self.end_monotonic - self.start_monotonic)


def format_control_command(verb: str, **fields: int) -> bytes:
    parts = [CONTROL_PREFIX.decode("ascii"), verb.upper()]
    for key, value in fields.items():
        parts.append(f"{key}={int(value)}")
    return (" ".join(parts) + "\n").encode("ascii")


def parse_control_response(payload: bytes) -> Dict[str, object]:
    try:
        line = payload.decode("ascii").strip()
    except UnicodeDecodeError as exc:
        raise ValueError("control response is not ASCII") from exc

    parts = line.split()
    if len(parts) < 2 or parts[0] != CONTROL_PREFIX.decode("ascii"):
        raise ValueError(f"invalid control response prefix: {line!r}")

    verb = parts[1]
    fields: Dict[str, object] = {}
    for token in parts[2:]:
        if "=" not in token:
            fields[token] = True
            continue
        key, value = token.split("=", 1)
        if value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
            fields[key] = int(value)
        else:
            fields[key] = value
    return {"verb": verb, "fields": fields, "raw": line}


def compute_throughput_kbps(bytes_received: int, duration_sec: float) -> float:
    if duration_sec <= 0:
        return 0.0
    return (bytes_received * 8.0) / duration_sec / 1000.0


def compute_packet_loss_pct(pkts_sent: int, pkts_received: int) -> Optional[float]:
    if pkts_sent <= 0:
        return None
    loss = max(0, pkts_sent - pkts_received)
    return (loss * 100.0) / pkts_sent


def make_data_packet(seq: int, payload_size: int, fill_byte: int = 0xA5) -> bytes:
    if payload_size < SEQ_HEADER_LEN:
        raise ValueError(f"payload_size must be >= {SEQ_HEADER_LEN}")
    return struct.pack("!I", seq) + bytes([fill_byte]) * (payload_size - SEQ_HEADER_LEN)


def extract_seq(packet: bytes) -> Optional[int]:
    if len(packet) < SEQ_HEADER_LEN:
        return None
    return struct.unpack("!I", packet[:SEQ_HEADER_LEN])[0]


class EndpointClient:
    def __init__(self, addr: str, port: int = DEFAULT_PORT, timeout_sec: float = DEFAULT_CONTROL_TIMEOUT_SEC):
        self.addr = addr
        self.port = port
        self.timeout_sec = timeout_sec

    def _send_control_and_recv(self, message: bytes) -> Dict[str, object]:
        family = socket.AF_INET6 if ":" in self.addr else socket.AF_INET
        with socket.socket(family, socket.SOCK_DGRAM) as sock:
            sock.settimeout(self.timeout_sec)
            sock.sendto(message, (self.addr, self.port))
            payload, _ = sock.recvfrom(2048)
        return parse_control_response(payload)

    def reset(self) -> Dict[str, object]:
        return self._send_control_and_recv(format_control_command("RESET"))

    def get_stats(self) -> Dict[str, object]:
        return self._send_control_and_recv(format_control_command("GET_STATS"))

    def start_tx(self, payload: int, duration_ms: int, dest_port: int, chunk_count: int) -> Dict[str, object]:
        return self._send_control_and_recv(
            format_control_command(
                "START_TX",
                payload=payload,
                duration_ms=duration_ms,
                dest_port=dest_port,
                chunk_count=chunk_count,
            )
        )


class UdpRxCollector:
    def __init__(self, bind_host: str, bind_port: int):
        self.bind_host = bind_host
        self.bind_port = bind_port
        self.metrics = TrialMetrics()

    def run(self, duration_sec: float, grace_sec: float = DEFAULT_RX_GRACE_SEC) -> TrialMetrics:
        family = socket.AF_INET6 if ":" in self.bind_host else socket.AF_INET
        with socket.socket(family, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((self.bind_host, self.bind_port))
            sock.setblocking(False)
            self.metrics.start_monotonic = time.monotonic()
            end_deadline = self.metrics.start_monotonic + duration_sec + grace_sec
            expected_next_seq: Optional[int] = None
            while True:
                now = time.monotonic()
                if now >= end_deadline:
                    break
                timeout = min(0.2, max(0.0, end_deadline - now))
                ready, _, _ = select.select([sock], [], [], timeout)
                if not ready:
                    continue
                packet, _ = sock.recvfrom(4096)
                self.metrics.bytes_received += len(packet)
                self.metrics.pkts_received += 1
                seq = extract_seq(packet)
                if seq is None:
                    continue
                if self.metrics.first_seq is None:
                    self.metrics.first_seq = seq
                self.metrics.last_seq = seq
                if expected_next_seq is not None and seq > expected_next_seq:
                    self.metrics.seq_gap_count += seq - expected_next_seq
                expected_next_seq = seq + 1
            self.metrics.end_monotonic = time.monotonic()
        return self.metrics


def run_tx_trial(addr: str, port: int, payload_size: int, duration_sec: float, poll_stats: bool) -> Dict[str, object]:
    family = socket.AF_INET6 if ":" in addr else socket.AF_INET
    metrics = TrialMetrics(start_monotonic=time.monotonic())
    seq = 0
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        sock.setblocking(False)
        deadline = metrics.start_monotonic + duration_sec
        while True:
            now = time.monotonic()
            if now >= deadline:
                break
            packet = make_data_packet(seq, payload_size)
            try:
                sent = sock.sendto(packet, (addr, port))
            except (BlockingIOError, InterruptedError):
                continue
            except OSError:
                break
            metrics.bytes_sent += sent
            metrics.pkts_sent += 1
            seq += 1
        metrics.end_monotonic = time.monotonic()

    result: Dict[str, object] = {
        "direction": "host_to_lamp",
        "payload_size": payload_size,
        "duration_sec": metrics.duration_sec,
        "bytes_sent": metrics.bytes_sent,
        "pkts_sent": metrics.pkts_sent,
        "throughput_kbps_tx": compute_throughput_kbps(metrics.bytes_sent, metrics.duration_sec),
    }

    if poll_stats:
        client = EndpointClient(addr, port)
        stats = client.get_stats()
        result["device_stats"] = stats
        fields = stats.get("fields", {}) if isinstance(stats, dict) else {}
        if isinstance(fields, dict):
            bytes_rx = int(fields.get("bytes_rx", 0))
            pkts_rx = int(fields.get("pkts_rx", 0))
            result["bytes_received"] = bytes_rx
            result["pkts_received"] = pkts_rx
            result["throughput_kbps_rx"] = compute_throughput_kbps(bytes_rx, metrics.duration_sec)
            result["packet_loss_pct"] = compute_packet_loss_pct(metrics.pkts_sent, pkts_rx)
            result["seq_gap_count"] = int(fields.get("seq_gap_count", 0))
    return result


def run_rx_trial(
    addr: str,
    port: int,
    bind_host: str,
    bind_port: int,
    payload_size: int,
    duration_sec: float,
    chunk_count: int,
) -> Dict[str, object]:
    client = EndpointClient(addr, port)
    client.reset()
    start_resp = client.start_tx(payload=payload_size, duration_ms=int(duration_sec * 1000), dest_port=bind_port, chunk_count=chunk_count)
    if start_resp.get("verb") != "OK":
        raise ValueError(f"START_TX failed: {start_resp}")

    collector = UdpRxCollector(bind_host=bind_host, bind_port=bind_port)
    metrics = collector.run(duration_sec)
    stats = client.get_stats()

    fields = stats.get("fields", {}) if isinstance(stats, dict) else {}
    bytes_tx = int(fields.get("bytes_tx", 0)) if isinstance(fields, dict) else 0
    pkts_tx = int(fields.get("pkts_tx", 0)) if isinstance(fields, dict) else 0

    return {
        "direction": "lamp_to_host",
        "payload_size": payload_size,
        "duration_sec": metrics.duration_sec,
        "bytes_received": metrics.bytes_received,
        "pkts_received": metrics.pkts_received,
        "throughput_kbps_rx": compute_throughput_kbps(metrics.bytes_received, metrics.duration_sec),
        "seq_gap_count_host": metrics.seq_gap_count,
        "device_stats": stats,
        "bytes_sent": bytes_tx,
        "pkts_sent": pkts_tx,
        "packet_loss_pct": compute_packet_loss_pct(pkts_tx, metrics.pkts_received),
    }


def _print_json(payload: Dict[str, object]) -> None:
    print(json.dumps(payload, sort_keys=True))


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="BL706 Thread UDP bandwidth helper")
    parser.add_argument("--addr", required=True, help="Target BL706 IPv6/IPv4 address")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"UDP control/data port (default: {DEFAULT_PORT})")

    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("reset", help="Reset device-side bandwidth counters and tx state")
    sub.add_parser("stats", help="Read device-side counters")

    tx = sub.add_parser("run-tx", help="Send host->lamp raw UDP traffic and report metrics")
    tx.add_argument("--payload", type=int, required=True)
    tx.add_argument("--duration", type=float, required=True)
    tx.add_argument("--skip-device-stats", action="store_true")

    rx = sub.add_parser("run-rx", help="Trigger lamp->host tx and receive on host")
    rx.add_argument("--payload", type=int, required=True)
    rx.add_argument("--duration", type=float, required=True)
    rx.add_argument("--bind-host", default="::")
    rx.add_argument("--bind-port", type=int, required=True)
    rx.add_argument("--chunk-count", type=int, default=8)

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    client = EndpointClient(args.addr, args.port)

    try:
        if args.cmd == "reset":
            _print_json(client.reset())
            return 0
        if args.cmd == "stats":
            _print_json(client.get_stats())
            return 0
        if args.cmd == "run-tx":
            client.reset()
            result = run_tx_trial(
                args.addr,
                args.port,
                payload_size=args.payload,
                duration_sec=args.duration,
                poll_stats=not args.skip_device_stats,
            )
            _print_json(result)
            return 0
        if args.cmd == "run-rx":
            result = run_rx_trial(
                args.addr,
                args.port,
                bind_host=args.bind_host,
                bind_port=args.bind_port,
                payload_size=args.payload,
                duration_sec=args.duration,
                chunk_count=args.chunk_count,
            )
            _print_json(result)
            return 0
    except (OSError, ValueError, TimeoutError) as exc:
        print(json.dumps({"error": str(exc)}), file=sys.stderr)
        return 2

    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
