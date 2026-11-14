#!/usr/bin/env python3
"""Stream this machine's controllers to the Urban Reign host as remote pads.

Each controller becomes one player slot: player 2 drives Pad2, player 3 drives
Pad3, and so on (the host maps player id N to SDL-(N-1)).

    python send_pad.py --list
    python send_pad.py 100.125.242.28 --player 2
    python send_pad.py 100.125.242.28 --player 2 3 --controller 0 1

Packets are 13-byte absolute-state datagrams at 120 Hz; nothing is
acknowledged. The overlay (Tailscale/ZeroTier) carries them.
"""

from __future__ import annotations

import argparse

from controller_protocol import (
    DEFAULT_PORT,
    DEFAULT_RATE,
    add_relay_arguments,
    controller_index_arg,
    list_controllers,
    player_id_arg,
    run_relay,
    validate_common_args,
)


def main() -> None:
    parser = argparse.ArgumentParser(description="Stream controllers to the game host as remote pads.")
    parser.add_argument("host", nargs="?", default="127.0.0.1", help="host address (default 127.0.0.1)")
    parser.add_argument("--player", nargs="+", type=player_id_arg, default=[2],
                        help="player id per controller (default 2)")
    parser.add_argument("--controller", nargs="+", type=controller_index_arg, default=[0],
                        help="pygame controller index per player (default 0)")
    add_relay_arguments(parser)
    args = parser.parse_args()

    if args.list:
        list_controllers()
        return
    validate_common_args(args)
    if len(args.player) != len(args.controller):
        parser.error("--player and --controller need the same number of entries")
    if len(set(args.player)) != len(args.player):
        parser.error("player ids must be unique")

    run_relay(args.player, args.controller, args.host, args)


if __name__ == "__main__":
    main()
