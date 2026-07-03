# UDPFS SMB Spoofer

Filename-compatible PS2 IOP driver stack for launchers that only know how to load the SMB module set:

- `poweroff.irx`
- `ps2dev9.irx`
- `smsutils.irx`
- `ps2ip.irx`
- `ps2smap.irx`
- `smbman.irx`

Internally this is UDPFS over Neutrino's SMAP/ministack path. `smbman.irx` registers the `smb:` ioman device, fakes both PS2SDK SMB devctl setup and SMS-style SMB ioctl setup, and lazily starts the bundled SMAP/ministack path when a real file operation needs UDPFS/UDPRDMA.

## Load Order

The expected SMB load order is preserved:

| SMB filename | Internal role |
| --- | --- |
| `poweroff.irx` | Real PS2SDK poweroff handler, including power-button shutdown support. |
| `ps2dev9.irx` | Real DEV9 hardware driver. |
| `smsutils.irx` | SMSUTILS compatibility module with helper exports and no-op RPC bind. |
| `ps2ip.irx` | Compatibility success module for the TCP/IP stack slot. |
| `ps2smap.irx` | Compatibility success module for the SMAP slot. |
| `smbman.irx` | Bundled SMAP/ministack plus UDPFS ioman driver registered as `smb:`; fakes SMB login/share setup before real UDPFS file IO. |

## Layout

- `__poweroff/` builds the real PS2SDK poweroff handler as `poweroff.irx`.
- `__ps2dev9/` builds the real DEV9 dependency as `ps2dev9.irx`.
- `__ps2ip_ministack/` holds the SMAP/ministack source and builds the lightweight `ps2smap.irx` compatibility module.
- `__smbman_udpfs/` builds the UDPFS ioman driver disguised as `smbman.irx`.
- `__ps2smap_stub/` builds the lightweight `ps2ip.irx` compatibility module.
- `__smsutils_stub/` is a compatibility module for SMSUTILS helper exports and the SMSUTILS RPC bind.
- `thirdparty/` is ignored and only used locally as source reference material.

## Build

Use a PS2SDK environment:

```sh
make clean package
```

GitHub Actions builds in `ps2dev/ps2dev:latest` and uploads the six IRX files plus `UDPFS_SMB_Spoofer.7z`.
