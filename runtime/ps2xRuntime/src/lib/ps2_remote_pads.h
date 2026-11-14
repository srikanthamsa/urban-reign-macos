#pragma once

// Controllers from other machines, as absolute-state UDP datagrams:
//
//   0  uint8   player_id (1..16; drives device player_id-1, so Pad2 = SDL-1)
//   1  uint16  button_mask (SDL standard layout, little endian)
//   3  int16   left_x
//   5  int16   left_y
//   7  int16   right_x
//   9  int16   right_y
//   11 uint8   left_trigger
//   12 uint8   right_trigger
//
// Every packet carries the whole state, so loss needs no recovery: the next packet corrects it.
// The overlay (ZeroTier/Tailscale) provides encryption; this listener authenticates nothing.
namespace ps2_remote_pads
{
    // PS2X_REMOTE_PADS=0 disables it; PS2X_REMOTE_PAD_PORT (50505) and PS2X_REMOTE_PAD_BIND (0.0.0.0).
    void start();
    [[nodiscard]] bool active(int device);
    [[nodiscard]] bool button(int device, int sdlButton);
    // Sticks -1..1 (down and right positive), triggers 0..1, as ps2_host_gamepads::axis.
    [[nodiscard]] float axis(int device, int sdlAxis);
    [[nodiscard]] int port();
}
