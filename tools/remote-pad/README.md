# remote-pad — controllers over the network

Sends a physical controller from a friend's PC to the game host as a normal pad, so nobody has to map buttons to
keys. It is the 13-byte absolute-state protocol from Nexus, unchanged:

```
offset  type     field
0       uint8    player_id        (2 -> Pad2, 3 -> Pad3, 4 -> Pad4 ...)
1       uint16   button_mask      (SDL standard layout, little endian)
3       int16    left_x
5       int16    left_y
7       int16    right_x
9       int16    right_y
11      uint8    left_trigger
12      uint8    right_trigger
```

Sticks are −32768…32767 (down/right positive), triggers 0…255. Every packet carries the whole state at 120 Hz, so UDP
loss needs no recovery: the next packet corrects it. There are no acknowledgements, sequence numbers or retries.

The host listens on **UDP 50505** and maps `player_id N` to `SDL-(N-1)`, which is what `controls.ini` binds
(`Pad2 = SDL-1`, `Pad3 = SDL-2`, …). A pad becomes "connected" while packets arrive and is released after 1.5 s
without them, so the local controller (if any) takes the slot back automatically.

## Friend: send a controller

Needs Python 3.9+ and [pygame](https://www.pygame.org/).

```sh
python -m pip install -r requirements.txt
python send_pad.py --list                          # controller indexes
python send_pad.py 100.125.242.28 --player 2       # one controller as player 2
python send_pad.py 100.125.242.28 --player 2 3 --controller 0 1   # two controllers
```

- `100.125.242.28` is the host's tailnet address; on the LAN use its local address.
- `--player` is the slot the host assigned (2, 3, 4 …), not the controller index.
- `--rate 120`, `--deadzone 0.08`, `--invert-y` and `--verbose` are available if a pad feels wrong.

The sender prints the mapping and keeps running until Ctrl-C. The game must be in a mode that uses that player slot.

## Host

The listener starts with the game (`[input] remote pads on 0.0.0.0:50505`). Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `PS2X_REMOTE_PADS` | on | `0` disables the listener |
| `PS2X_REMOTE_PAD_PORT` | `50505` | UDP port |
| `PS2X_REMOTE_PAD_BIND` | `0.0.0.0` | bind address; set the tailnet IP to ignore LAN traffic |

In-game hotkeys and the keyboard clusters keep working at the same time; a pad takes precedence only for its own slot
while packets arrive.

## Security

The overlay (Tailscale or ZeroTier) provides encryption and membership. The listener itself authenticates nothing: any
correctly sized datagram that reaches the port sets that player's state. Use it on a trusted network, bind it to the
overlay address if the Mac is also on untrusted Wi-Fi, and don't expose 50505 to the internet.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `No controllers detected` | Connect the pad, press a button, rerun with `--list`. |
| `pygame is required` | `python -m pip install -r requirements.txt` |
| Host log never shows `remote pad N` | Wrong address/port, or the overlay is down: `ping <host>` first. |
| Pad works but the character doesn't move | Wrong player id for that mode, or the mode doesn't use that slot. |
| Sticks drift | Raise `--deadzone`; if Y is inverted, add `--invert-y`. |
