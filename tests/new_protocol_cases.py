from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time
from typing import Any


EDIT_CARD_CONTENT = 0x03
CREATE_CARD = 0x04
SYNC_BOARD = 0x09
DISCONNECT = 0x10
LIST_BOARDS = 0x16
BOARD_LIST = 0x17
ERROR = 0xFF
MAX_PACKET_PAYLOAD_BYTES = 10 * 1024 * 1024
IGNORED_NOTIFICATIONS = {0x0A, 0x0B, 0x20}


class TestFailure(RuntimeError):
    """Raised when a protocol case does not meet its expected result."""


def encode_frame(opcode: int, payload: dict[str, Any] | bytes) -> bytes:
    if isinstance(payload, bytes):
        body = payload
    else:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(body) > MAX_PACKET_PAYLOAD_BYTES:
        raise ValueError("payload exceeds the server's 10 MiB limit")
    return bytes((opcode,)) + struct.pack("!I", len(body)) + body


def receive_exact(sock: socket.socket, size: int) -> bytes:
    parts: list[bytes] = []
    remaining = size
    while remaining:
        part = sock.recv(remaining)
        if not part:
            raise EOFError("server closed the connection before the full packet arrived")
        parts.append(part)
        remaining -= len(part)
    return b"".join(parts)


class ProtocolClient:
    def __init__(self, host: str, port: int, timeout: float = 4.0) -> None:
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.timeout = timeout
        self.closed = False

    def send(self, opcode: int, payload: dict[str, Any] | bytes) -> None:
        self.sock.sendall(encode_frame(opcode, payload))

    def receive(self, timeout: float | None = None) -> tuple[int, Any, bytes]:
        previous_timeout = self.sock.gettimeout()
        self.sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            header = receive_exact(self.sock, 5)
            payload_size = struct.unpack("!I", header[1:])[0]
            if payload_size > MAX_PACKET_PAYLOAD_BYTES:
                raise TestFailure(f"server returned an oversized payload: {payload_size} bytes")
            raw = receive_exact(self.sock, payload_size) if payload_size else b""
            try:
                payload: Any = json.loads(raw.decode("utf-8")) if raw else {}
            except (UnicodeDecodeError, json.JSONDecodeError):
                payload = raw
            return header[0], payload, raw
        finally:
            self.sock.settimeout(previous_timeout)

    def receive_expected(
        self,
        expected_opcodes: set[int],
        timeout: float = 4.0,
    ) -> tuple[int, Any, bytes]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for opcode(s) {sorted(expected_opcodes)}")
            response = self.receive(remaining)
            if response[0] in expected_opcodes:
                return response
            if response[0] in IGNORED_NOTIFICATIONS:
                continue
            raise TestFailure(
                f"received opcode 0x{response[0]:02X} while waiting for "
                f"{[f'0x{opcode:02X}' for opcode in sorted(expected_opcodes)]}: {response[1]}"
            )

    def request(
        self,
        opcode: int,
        payload: dict[str, Any] | bytes,
        expected_opcodes: set[int],
        timeout: float = 4.0,
    ) -> tuple[int, Any, bytes]:
        self.send(opcode, payload)
        return self.receive_expected(expected_opcodes, timeout)

    def sync_board(self, board_id: int) -> dict[str, Any]:
        opcode, payload, _ = self.request(
            SYNC_BOARD, {"boardID": board_id}, {SYNC_BOARD, ERROR}
        )
        if opcode != SYNC_BOARD or not isinstance(payload, dict):
            raise TestFailure(f"SYNC_BOARD failed: opcode=0x{opcode:02X}, payload={payload}")
        return payload

    def close(self, disconnect: bool = True) -> None:
        if self.closed:
            return
        if disconnect:
            try:
                self.send(DISCONNECT, {})
            except OSError:
                pass
        try:
            self.sock.close()
        finally:
            self.closed = True

    def __enter__(self) -> ProtocolClient:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

def require_ok(response: tuple[int, Any, bytes], expected_opcode: int, action: str) -> dict[str, Any]:
    opcode, payload, _ = response
    if opcode != expected_opcode or not isinstance(payload, dict):
        raise TestFailure(f"{action} failed: opcode=0x{opcode:02X}, payload={payload}")
    return payload

def run_tc122(args: argparse.Namespace) -> str:
    with ProtocolClient(args.host, args.port) as client_a, ProtocolClient(args.host, args.port) as client_b:
        client_a.sync_board(args.board_id)
        client_b.sync_board(args.board_id)
        with ProtocolClient(args.host, args.port) as malformed_client:
            malformed_client.send(LIST_BOARDS, b"{not-json}")
            error = require_ok(
                malformed_client.receive_expected({ERROR}), ERROR, "malformed JSON request"
            )
            if error.get("errCode") != 400:
                raise TestFailure(f"malformed JSON returned {error.get('errCode')}, expected 400")

            text = f"TC-122 isolation {time.time_ns()}"
            created = require_ok(
                client_a.request(
                    CREATE_CARD,
                    {"x": 120, "y": 120, "width": 120, "height": 90, "text": text},
                    {CREATE_CARD, ERROR},
                ),
                CREATE_CARD,
                "valid create after malformed request",
            )
            card_id = created.get("cardID")
            if not isinstance(card_id, int) or card_id <= 0:
                raise TestFailure(f"server did not assign a valid card ID: {created}")
            peer_update = require_ok(
                client_b.receive_expected({CREATE_CARD, ERROR}),
                CREATE_CARD,
                "peer's card update",
            )
            if peer_update.get("cardID") != card_id:
                raise TestFailure(f"normal clients received different card IDs: {created}, {peer_update}")
            return f"malformed request returned 400; clients stayed connected and received card ID {card_id}"


def rejected_connection(host: str, port: int) -> None:
    probe: ProtocolClient | None = None
    try:
        probe = ProtocolClient(host, port, timeout=2.0)
        probe.send(LIST_BOARDS, {})
        try:
            response = probe.receive(2.0)
        except (EOFError, ConnectionResetError, BrokenPipeError):
            return
        except socket.timeout as exc:
            raise TestFailure("17th connection timed out instead of being clearly refused") from exc
        raise TestFailure(
            f"17th connection received opcode 0x{response[0]:02X}; expected it to be refused"
        )
    except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError):
        return
    finally:
        if probe is not None:
            probe.close(disconnect=False)


def connect_after_slot_freed(host: str, port: int, timeout: float = 5.0) -> ProtocolClient:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        candidate: ProtocolClient | None = None
        try:
            candidate = ProtocolClient(host, port, timeout=1.0)
            response = candidate.request(LIST_BOARDS, {}, {BOARD_LIST, ERROR}, timeout=1.0)
            if response[0] == BOARD_LIST:
                return candidate
            raise TestFailure(f"new connection returned an error: {response[1]}")
        except (OSError, EOFError, socket.timeout) as exc:
            last_error = exc
            if candidate is not None:
                candidate.close(disconnect=False)
            time.sleep(0.1)
    raise TestFailure(f"a connection slot did not become available: {last_error}")


def run_tc123(args: argparse.Namespace) -> str:
    active: list[ProtocolClient] = []
    try:
        for number in range(1, 17):
            client = ProtocolClient(args.host, args.port)
            active.append(client)
            response = client.request(LIST_BOARDS, {}, {BOARD_LIST, ERROR})
            if response[0] != BOARD_LIST:
                raise TestFailure(f"connection {number} was not usable: {response[1]}")

        rejected_connection(args.host, args.port)
        released = active.pop(0)
        released.close()
        replacement = connect_after_slot_freed(args.host, args.port)
        active.append(replacement)
        return "16 connections served LIST_BOARDS; the 17th was refused; a new client connected after one slot was freed"
    finally:
        for client in active:
            client.close()


def run_tc124(args: argparse.Namespace) -> str:
    with ProtocolClient(args.host, args.port) as client:
        client.sync_board(args.board_id)
        token = str(time.time_ns())
        card_texts = [f"TC-124 {token} card {index}" for index in range(1, 6)]
        frames = [
            encode_frame(
                CREATE_CARD,
                {
                    "x": 100 + index * 140,
                    "y": 180,
                    "width": 120,
                    "height": 90,
                    "text": card_text,
                },
            )
            for index, card_text in enumerate(card_texts)
        ]
        client.sock.sendall(b"".join(frames))

        card_ids: list[int] = []
        for expected_text in card_texts:
            response = require_ok(
                client.receive_expected({CREATE_CARD, ERROR}),
                CREATE_CARD,
                "CREATE_CARD in the burst",
            )
            card_id = response.get("cardID")
            if not isinstance(card_id, int) or card_id <= 0:
                raise TestFailure(f"server did not return a positive card ID: {response}")
            if response.get("text") != expected_text:
                raise TestFailure(
                    f"CREATE_CARD replies were out of order: expected {expected_text!r}, got {response.get('text')!r}"
                )
            card_ids.append(card_id)
        if len(set(card_ids)) != 5:
            raise TestFailure(f"card IDs were not unique: {card_ids}")

        snapshot = client.sync_board(args.board_id)
        cards = snapshot.get("cards", [])
        seen = {card.get("text") for card in cards if isinstance(card, dict)}
        if not set(card_texts).issubset(seen):
            raise TestFailure("the board snapshot did not contain all five cards from the burst")
        return f"one TCP write carried five CREATE_CARD frames; replies had unique IDs {card_ids}; snapshot contained all five cards"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("TC-118", "TC-122", "TC-123", "TC-124"))
    parser.add_argument("--host", default="127.0.0.1", help="test server hostname or IP (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, required=True, help="test server TCP port")
    parser.add_argument("--board-id", type=int, help="disposable board ID for TC-118, TC-122, or TC-124")
    parser.add_argument("--card-id", type=int, help="existing card ID for TC-118")
    parser.add_argument(
        "--confirm-disposable-board",
        action="store_true",
        help="confirm that mutating cases may edit or create data on this disposable board",
    )
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.case != "TC-123":
        if not args.board_id or args.board_id <= 0:
            parser.error(f"--board-id is required for {args.case}")
        if not args.confirm_disposable_board:
            parser.error(f"{args.case} changes test-board data; pass --confirm-disposable-board")
    if args.case == "TC-118" and (not args.card_id or args.card_id <= 0):
        parser.error("--card-id is required for TC-118")
    return args


def main() -> int:
    args = parse_args()
    runners = {
        "TC-118": run_tc118,
        "TC-122": run_tc122,
        "TC-123": run_tc123,
        "TC-124": run_tc124,
    }
    try:
        result = runners[args.case](args)
    except (OSError, EOFError, socket.timeout, TestFailure, ValueError) as exc:
        print(f"[{args.case}] FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"[{args.case}] PASS: {result}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
