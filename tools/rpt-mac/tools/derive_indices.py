#!/usr/bin/env python3
"""Re-derive IClientRemoteClientManager vtable indices after a Steam update.

Steam ships a fat Mach-O; this reads the arm64 slice of steamclient.dylib,
builds the vtable slot -> function map from a live process dump, then finds
which slot contains a code reference to each method-name string.

  1. ./rptmac dump > /tmp/vt.txt          (needs Steam running)
  2. lipo -thin arm64 steamclient.dylib -output /tmp/sc_arm64.dylib
  3. ./tools/derive_indices.py /tmp/sc_arm64.dylib /tmp/vt.txt
"""
import re
import struct
import sys

NAMES = [
    "BHasRemotePlayInviteAndSession", "BCreateRemotePlayGroup",
    "GetLocalRemotePlayTogetherGroupID", "GetRemotePlayTogetherGroupIDForOverlayPID",
    "GetAvailableRemotePlayTogetherGuestID", "BCreateRemotePlayInviteAndSession",
    "CancelRemotePlayInviteAndSession", "SetStreamingDesktopToRemotePlayTogetherEnabled",
    "SetPerUserKeyboardInputEnabled", "SetPerUserMouseInputEnabled",
    "SetPerUserControllerInputEnabled", "UpdateRemotePlayTogetherGroups",
    "UpdateRemotePlayTogetherGroup", "DisbandRemotePlayTogetherGroup",
    "ShowRemotePlayTogetherUI", "BIsStreamingEnabled", "GetStreamingPINSize",
]


def main(dylib_path, vt_path):
    data = open(dylib_path, "rb").read()
    vt = {}
    for line in open(vt_path):
        m = re.match(r"(\d+) 0x([0-9a-f]+)$", line.strip())
        if m:
            vt[int(m.group(1))] = int(m.group(2), 16)

    def slot_for(addr):
        best = None
        for idx, start in sorted(vt.items()):
            if start <= addr:
                best = idx
            else:
                break
        return best

    found = {}
    for name in NAMES:
        raw = name.encode()
        addrs = set()
        i = data.find(raw)
        while i != -1:
            if data[i - 1] == 0 and data[i + len(raw)] == 0:
                addrs.add(i)
            i = data.find(raw, i + 1)
        slot = None
        for off in range(0x8000, 0x1316604, 4):
            w = struct.unpack_from("<I", data, off)[0]
            if (w & 0x9F000000) != 0x90000000:
                continue
            imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 0x3)
            if imm & (1 << 20):
                imm -= 1 << 21
            page = (off & ~0xFFF) + (imm << 12)
            for k in (1, 2, 3):
                w2 = struct.unpack_from("<I", data, off + k * 4)[0]
                if (w2 & 0xFF800000) == 0x91000000:
                    target = page + ((w2 >> 10) & 0xFFF)
                    if target in addrs and off > 0x950000:
                        slot = slot_for(off)
        found[name] = slot
        print(f"{name:48s} {slot}")

    expected = {"BCreateRemotePlayGroup": 122, "GetLocalRemotePlayTogetherGroupID": 123,
                "GetAvailableRemotePlayTogetherGuestID": 125, "BCreateRemotePlayInviteAndSession": 126}
    drift = {k: v for k, v in found.items() if k in expected and v != expected[k]}
    if drift:
        print("\nIndices changed - update the enum in rptmac.c:", drift)
        return 1
    print("\nIndices match rptmac.c")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
