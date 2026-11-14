#!/usr/bin/env python3
"""Shared UDP controller protocol and relay loop.

All clients send the same 13-byte little-endian datagram:

    offset  type      field
    0       uint8     player_id
    1       uint16    button_mask
    3       int16     left_x
    5       int16     left_y
    7       int16     right_x
    9       int16     right_y
    11      uint8     left_trigger
    12      uint8     right_trigger

Sticks are -32768..32767. X is negative left and positive right. Y is
negative up and positive down by default, matching SDL/pygame; use --invert-y
to send positive-up instead.
"""

from __future__ import annotations

import argparse
import ctypes
import math
import os
import socket
import struct
import sys
import time
from dataclasses import dataclass
from enum import IntFlag
from typing import List, Optional, Sequence, Tuple

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")

PACKET = struct.Struct("<BHhhhhBB")  # player, buttons, LX, LY, RX, RY, LT, RT
PACKET_SIZE = PACKET.size
DEFAULT_HOST = "192.168.195.252"  # ZeroTier managed IP for network a09acf02332a5b30
DEFAULT_PORT = 50505
DEFAULT_RATE = 120.0

assert PACKET_SIZE == 13


class Button(IntFlag):
    A = 1 << 0
    B = 1 << 1
    X = 1 << 2
    Y = 1 << 3
    LB = 1 << 4
    RB = 1 << 5
    BACK = 1 << 6
    START = 1 << 7
    GUIDE = 1 << 8
    LEFT_STICK = 1 << 9
    RIGHT_STICK = 1 << 10
    DPAD_UP = 1 << 11
    DPAD_DOWN = 1 << 12
    DPAD_LEFT = 1 << 13
    DPAD_RIGHT = 1 << 14


# SDL standard game-controller button indices.
SDL_BUTTON_BITS = (
    (0, Button.A),
    (1, Button.B),
    (2, Button.X),
    (3, Button.Y),
    (4, Button.LB),
    (5, Button.RB),
    (6, Button.BACK),
    (7, Button.START),
    (8, Button.GUIDE),
    (9, Button.LEFT_STICK),
    (10, Button.RIGHT_STICK),
    (11, Button.DPAD_UP),
    (12, Button.DPAD_DOWN),
    (13, Button.DPAD_LEFT),
    (14, Button.DPAD_RIGHT),
)

BUTTON_NAMES = (
    (Button.A, "A"),
    (Button.B, "B"),
    (Button.X, "X"),
    (Button.Y, "Y"),
    (Button.LB, "LB"),
    (Button.RB, "RB"),
    (Button.BACK, "Back"),
    (Button.START, "Start"),
    (Button.GUIDE, "Guide"),
    (Button.LEFT_STICK, "LS"),
    (Button.RIGHT_STICK, "RS"),
    (Button.DPAD_UP, "Up"),
    (Button.DPAD_DOWN, "Down"),
    (Button.DPAD_LEFT, "Left"),
    (Button.DPAD_RIGHT, "Right"),
)


def clamp_i16(value: float) -> int:
    return max(-32768, min(32767, int(round(value))))


def pack_packet(
    player_id: int,
    buttons: int,
    left_x: int,
    left_y: int,
    right_x: int,
    right_y: int,
    left_trigger: int,
    right_trigger: int,
) -> bytes:
    """Pack one player state. Callers should keep axes/triggers in range."""
    if not 1 <= player_id <= 255:
        raise ValueError("player_id must be 1..255")
    return PACKET.pack(
        player_id,
        buttons & 0xFFFF,
        clamp_i16(left_x),
        clamp_i16(left_y),
        clamp_i16(right_x),
        clamp_i16(right_y),
        max(0, min(255, int(left_trigger))),
        max(0, min(255, int(right_trigger))),
    )


def unpack_packet(data: bytes) -> Tuple[int, int, int, int, int, int, int, int]:
    if len(data) != PACKET_SIZE:
        raise ValueError(f"expected {PACKET_SIZE} bytes, got {len(data)}")
    return PACKET.unpack(data)


@dataclass
class ControllerState:
    buttons: int = 0
    left_x: int = 0
    left_y: int = 0
    right_x: int = 0
    right_y: int = 0
    left_trigger: int = 0
    right_trigger: int = 0

    def packet(self, player_id: int) -> bytes:
        return pack_packet(
            player_id,
            self.buttons,
            self.left_x,
            self.left_y,
            self.right_x,
            self.right_y,
            self.left_trigger,
            self.right_trigger,
        )


@dataclass(frozen=True)
class AxisMap:
    left_x: int
    left_y: int
    right_x: int
    right_y: int
    left_trigger: int = -1
    right_trigger: int = -1


def normalize_stick(
    x: float, y: float, deadzone: float = 0.0, invert_y: bool = False
) -> Tuple[int, int]:
    """Apply a radial deadzone and return signed 16-bit stick values."""
    x = max(-1.0, min(1.0, float(x)))
    y = max(-1.0, min(1.0, float(y)))
    if invert_y:
        y = -y

    magnitude = math.hypot(x, y)
    if magnitude < 1e-9:
        return 0, 0
    if magnitude > 1.0:
        x /= magnitude
        y /= magnitude
        magnitude = 1.0
    if deadzone > 0.0:
        if magnitude <= deadzone:
            return 0, 0
        scaled = (magnitude - deadzone) / (1.0 - deadzone)
    else:
        scaled = magnitude

    unit_x = x / magnitude
    unit_y = y / magnitude
    out_x = unit_x * scaled
    out_y = unit_y * scaled
    # Use the full signed 16-bit range: -32768..32767.
    return (
        clamp_i16(out_x * (32768.0 if out_x < 0 else 32767.0)),
        clamp_i16(out_y * (32768.0 if out_y < 0 else 32767.0)),
    )


def trigger_to_byte(value: float, rest: float = -1.0) -> int:
    """Convert a trigger axis to 0..255.

    SDL reports trigger axes as -1 at rest and +1 fully pressed. Some
    backends/drivers report 0 at rest instead, so ``rest`` is configurable.
    """
    if rest <= -0.5:
        normalized = (float(value) + 1.0) * 0.5
    else:
        normalized = float(value)
    return max(0, min(255, int(round(normalized * 255.0))))


def parse_axis_map(text: str) -> Optional[AxisMap]:
    if text.strip().lower() == "auto":
        return None
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 6:
        raise argparse.ArgumentTypeError(
            "axis map must be 'auto' or LX,LY,RX,RY,LT,RT (use -1 to disable a trigger)"
        )
    try:
        values = [int(p) for p in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("axis map entries must be integers") from exc
    if any(v < -1 or v > 31 for v in values):
        raise argparse.ArgumentTypeError("axis indices must be -1..31")
    if len({values[0], values[1], values[2], values[3]}) != 4:
        raise argparse.ArgumentTypeError("LX, LY, RX and RY must use distinct axes")
    return AxisMap(*values)


def auto_axis_map(axis_count: int) -> Tuple[AxisMap, Optional[int]]:
    """Pick a sensible map from the controller's axis count.

    Returns (map, combined_trigger_axis). Six or more axes is the SDL standard
    layout. Five axes commonly means an older Xbox 360 mapping where the right
    stick is on axes 3/4 and axis 2 carries both triggers.
    """
    if axis_count >= 6:
        return AxisMap(0, 1, 2, 3, 4, 5), None
    if axis_count == 5:
        return AxisMap(0, 1, 3, 4, 2, -1), 2
    if axis_count >= 4:
        return AxisMap(0, 1, 2, 3), None
    return AxisMap(0, 1, 0, 1), None


def positive_rate(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("rate must be a number") from exc
    if not 1.0 <= value <= 1000.0:
        raise argparse.ArgumentTypeError("rate must be between 1 and 1000 Hz")
    return value


def player_id_arg(text: str) -> int:
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("player ID must be an integer") from exc
    if not 1 <= value <= 255:
        raise argparse.ArgumentTypeError("player ID must be 1..255")
    return value


def controller_index_arg(text: str) -> int:
    try:
        value = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("controller index must be an integer") from exc
    if value < 0:
        raise argparse.ArgumentTypeError("controller index cannot be negative")
    return value


def deadzone_arg(text: str) -> float:
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("deadzone must be a number") from exc
    if not 0.0 <= value < 1.0:
        raise argparse.ArgumentTypeError("deadzone must be >= 0 and < 1")
    return value


def trigger_rest_arg(text: str) -> Optional[float]:
    if text.strip().lower() == "auto":
        return None
    try:
        value = float(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("trigger rest must be 'auto', -1 or 0") from exc
    if value not in (-1.0, 0.0):
        raise argparse.ArgumentTypeError("trigger rest must be 'auto', -1 or 0")
    return value


class RateLimiter:
    """Fixed-rate scheduler with a short final spin for low jitter."""

    def __init__(self, hz: float) -> None:
        if hz <= 0:
            raise ValueError("rate must be positive")
        self.period = 1.0 / hz
        self.hz = hz
        self._next: Optional[float] = None
        self._timer_active = False

    def __enter__(self) -> "RateLimiter":
        if sys.platform == "win32":
            try:
                ctypes.windll.winmm.timeBeginPeriod(1)  # type: ignore[attr-defined]
                self._timer_active = True
            except Exception:
                self._timer_active = False
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._timer_active:
            try:
                ctypes.windll.winmm.timeEndPeriod(1)  # type: ignore[attr-defined]
            except Exception:
                pass
            self._timer_active = False

    def wait(self) -> None:
        now = time.perf_counter()
        if self._next is None:
            self._next = now
        if now < self._next:
            remaining = self._next - now
            if remaining > 0.001:
                time.sleep(remaining - 0.001)
            while time.perf_counter() < self._next:
                pass
        self._next += self.period
        # Do not spend forever catching up after a laptop sleep or breakpoint.
        now = time.perf_counter()
        if now - self._next > 1.0:
            self._next = now + self.period


class UDPSender:
    def __init__(self, host: str, port: int) -> None:
        try:
            infos = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
        except socket.gaierror as exc:
            raise SystemExit(f"Cannot resolve host {host!r}: {exc}") from None
        if not infos:
            raise SystemExit(f"Cannot resolve host {host!r}")
        family, socktype, proto, _, address = infos[0]
        self.address = address
        self.target = f"{host}:{port}"
        self.sock = socket.socket(family, socktype, proto)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024)
        except OSError:
            pass
        self._last_error = 0.0

    def send(self, payload: bytes) -> None:
        try:
            self.sock.sendto(payload, self.address)
        except OSError as exc:
            now = time.monotonic()
            if now - self._last_error >= 5.0:
                print(f"UDP send error: {exc}", file=sys.stderr)
                self._last_error = now

    def close(self) -> None:
        self.sock.close()


def _pygame():
    try:
        import pygame  # type: ignore
    except ImportError:
        raise SystemExit(
            "pygame is required. Install it with:\n"
            "    python -m pip install pygame"
        ) from None
    return pygame


def list_controllers() -> None:
    pygame = _pygame()
    pygame.init()
    pygame.joystick.init()
    pygame.event.pump()
    count = pygame.joystick.get_count()
    print(f"Found {count} controller(s)")
    for index in range(count):
        joy = pygame.joystick.Joystick(index)
        joy.init()
        print(
            f"  [{index}] {joy.get_name()} | "
            f"axes={joy.get_numaxes()} buttons={joy.get_numbuttons()} "
            f"hats={joy.get_numhats()}"
        )
        joy.quit()
    if count == 0:
        print("Connect a controller, then run again. On macOS check Input Monitoring.")
    pygame.quit()


class PygameController:
    """One pygame joystick, including reconnect and trigger auto-calibration."""

    def __init__(
        self,
        index: int,
        player_id: int,
        axis_map: Optional[AxisMap] = None,
        combined_trigger_axis: Optional[int] = None,
        trigger_rest: Optional[float] = None,
        deadzone: float = 0.08,
        invert_y: bool = False,
    ) -> None:
        self.index = index
        self.player_id = player_id
        self._axis_map_arg = axis_map
        self._combined_arg = combined_trigger_axis
        self._trigger_rest_arg = trigger_rest
        self.deadzone = deadzone
        self.invert_y = invert_y
        self.joy = None
        self.axis_map = axis_map
        self.combined_trigger_axis = combined_trigger_axis
        self._trigger_rest: List[Optional[float]] = [trigger_rest, trigger_rest]
        self._next_retry = 0.0
        self._warned_missing_axis = False

    def connect(self) -> bool:
        pygame = _pygame()
        if pygame.joystick.get_count() <= self.index:
            return False
        try:
            joy = pygame.joystick.Joystick(self.index)
            joy.init()
        except pygame.error as exc:
            self._retry_later(f"controller {self.index}: {exc}")
            return False

        self.joy = joy
        if self._axis_map_arg is None:
            self.axis_map, auto_combined = auto_axis_map(joy.get_numaxes())
            if self._combined_arg is None:
                self.combined_trigger_axis = auto_combined
        else:
            self.axis_map = self._axis_map_arg
            self.combined_trigger_axis = self._combined_arg
            if self.axis_map.left_trigger >= 0 or self.axis_map.right_trigger >= 0:
                self._trigger_rest = [self._trigger_rest_arg, self._trigger_rest_arg]

        print(
            f"Player {self.player_id} <- controller [{self.index}] "
            f"{joy.get_name()} "
            f"({joy.get_numaxes()} axes, {joy.get_numbuttons()} buttons)"
        )
        return True

    def _retry_later(self, message: str) -> None:
        now = time.monotonic()
        if now >= self._next_retry:
            print(f"Waiting for {message}", file=sys.stderr)
            self._next_retry = now + 1.0

    def _axis(self, index: int) -> float:
        if self.joy is None or index < 0 or index >= self.joy.get_numaxes():
            if not self._warned_missing_axis:
                print(
                    f"Controller {self.index} has no axis {index}; "
                    "check --axis-map.",
                    file=sys.stderr,
                )
                self._warned_missing_axis = True
            return 0.0
        return float(self.joy.get_axis(index))

    def _calibrate_trigger(self, slot: int, raw: float) -> float:
        if self._trigger_rest[slot] is None:
            # SDL normally rests at -1. A value near 0 means this driver uses
            # 0..1 triggers. Choosing 0 still degrades safely to clamped 0..255
            # if the stick happens to be held at startup.
            self._trigger_rest[slot] = -1.0 if raw < -0.2 else 0.0
        return float(self._trigger_rest[slot])

    def read(self) -> Optional[ControllerState]:
        pygame = _pygame()
        if self.joy is None or not self.joy.get_init():
            if time.monotonic() < self._next_retry:
                return None
            if not self.connect():
                return None

        assert self.joy is not None
        assert self.axis_map is not None
        try:
            raw_lx = self._axis(self.axis_map.left_x)
            raw_ly = self._axis(self.axis_map.left_y)
            raw_rx = self._axis(self.axis_map.right_x)
            raw_ry = self._axis(self.axis_map.right_y)
            lx, ly = normalize_stick(
                raw_lx, raw_ly, self.deadzone, self.invert_y
            )
            rx, ry = normalize_stick(
                raw_rx, raw_ry, self.deadzone, self.invert_y
            )

            if self.combined_trigger_axis is not None:
                raw_trigger = self._axis(self.combined_trigger_axis)
                lt = max(0, min(255, int(round(max(0.0, -raw_trigger) * 255))))
                rt = max(0, min(255, int(round(max(0.0, raw_trigger) * 255))))
            else:
                lt = 0
                rt = 0
                if self.axis_map.left_trigger >= 0:
                    raw_lt = self._axis(self.axis_map.left_trigger)
                    lt = trigger_to_byte(
                        raw_lt, self._calibrate_trigger(0, raw_lt)
                    )
                if self.axis_map.right_trigger >= 0:
                    raw_rt = self._axis(self.axis_map.right_trigger)
                    rt = trigger_to_byte(
                        raw_rt, self._calibrate_trigger(1, raw_rt)
                    )

            mask = 0
            button_count = self.joy.get_numbuttons()
            for button_index, bit in SDL_BUTTON_BITS:
                if button_index < button_count and self.joy.get_button(button_index):
                    mask |= int(bit)

            for hat_index in range(self.joy.get_numhats()):
                hx, hy = self.joy.get_hat(hat_index)
                if hx < 0:
                    mask |= int(Button.DPAD_LEFT)
                elif hx > 0:
                    mask |= int(Button.DPAD_RIGHT)
                if hy > 0:
                    mask |= int(Button.DPAD_UP)
                elif hy < 0:
                    mask |= int(Button.DPAD_DOWN)
        except pygame.error as exc:
            self._retry_later(f"controller {self.index}: {exc}")
            try:
                self.joy.quit()
            except Exception:
                pass
            self.joy = None
            return None

        return ControllerState(mask, lx, ly, rx, ry, lt, rt)


def add_relay_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"UDP port on the host Mac (default {DEFAULT_PORT})")
    parser.add_argument("--rate", type=positive_rate, default=DEFAULT_RATE,
                        help=f"poll/send rate in Hz (default {DEFAULT_RATE:g})")
    parser.add_argument("--deadzone", type=deadzone_arg, default=0.08,
                        help="radial stick deadzone, 0..1 (default 0.08)")
    parser.add_argument("--invert-y", action="store_true",
                        help="send Y as positive-up instead of the SDL default")
    parser.add_argument("--axis-map", type=parse_axis_map, default=None,
                        metavar="LX,LY,RX,RY,LT,RT",
                        help="override axis indices; use -1 for no trigger axis")
    parser.add_argument("--combined-trigger", type=int, default=None,
                        metavar="AXIS",
                        help="axis carrying both triggers (-1 at rest, +1 right)")
    parser.add_argument("--trigger-rest", type=trigger_rest_arg, default=None,
                        help="trigger rest value: auto, -1 (default SDL) or 0")
    parser.add_argument("--verbose", action="store_true",
                        help="print packet rate every 5 seconds")
    parser.add_argument("--list", action="store_true",
                        help="list connected controllers and exit")


def validate_common_args(args: argparse.Namespace) -> None:
    if not 1 <= args.port <= 65535:
        raise SystemExit("--port must be 1..65535")
    if args.combined_trigger is not None and args.combined_trigger < 0:
        raise SystemExit("--combined-trigger must be >= 0")


def run_relay(
    player_ids: Sequence[int],
    controller_indices: Sequence[int],
    host: str,
    args: argparse.Namespace,
) -> None:
    if len(player_ids) != len(controller_indices):
        raise ValueError("each player needs one controller index")
    if len(set(player_ids)) != len(player_ids):
        raise ValueError("player IDs must be unique")

    pygame = _pygame()
    pygame.init()
    pygame.joystick.init()
    pygame.event.pump()

    if pygame.joystick.get_count() == 0:
        raise SystemExit(
            "No controllers detected. Connect one and run again, or use --list."
        )

    controllers: List[PygameController] = []
    for player_id, controller_index in zip(player_ids, controller_indices):
        controller = PygameController(
            index=controller_index,
            player_id=player_id,
            axis_map=args.axis_map,
            combined_trigger_axis=args.combined_trigger,
            trigger_rest=args.trigger_rest,
            deadzone=args.deadzone,
            invert_y=args.invert_y,
        )
        if not controller.connect():
            raise SystemExit(
                f"Controller index {controller_index} is not available. "
                "Run with --list to see connected controllers."
            )
        controllers.append(controller)

    sender = UDPSender(host, args.port)
    mapping = ", ".join(
        f"P{pid}=controller[{idx}]"
        for pid, idx in zip(player_ids, controller_indices)
    )
    print(
        f"Sending {mapping} -> {sender.target} at {args.rate:g} Hz "
        f"({PACKET_SIZE}-byte packets)"
    )

    sent = 0
    report_at = time.monotonic() + 5.0
    last_report_count = 0
    limiter = RateLimiter(args.rate)
    try:
        with limiter:
            while True:
                pygame.event.pump()
                limiter.wait()
                for controller in controllers:
                    state = controller.read()
                    if state is not None:
                        sender.send(state.packet(controller.player_id))
                        sent += 1
                if args.verbose and time.monotonic() >= report_at:
                    elapsed = 5.0
                    rate = (sent - last_report_count) / elapsed
                    print(f"{sent} packets sent ({rate:.1f} packets/s)")
                    last_report_count = sent
                    report_at += 5.0
    except KeyboardInterrupt:
        print(f"\nStopped after {sent} packets.")
    finally:
        sender.close()
        pygame.quit()


def print_buttons(mask: int) -> str:
    return "+".join(name for bit, name in BUTTON_NAMES if mask & int(bit)) or "-"
