# UDPFS SMB Spoofer

Filename-compatible PS2 IOP driver stack for launchers that only know how to load the SMB module set:

- `poweroff.irx`
- `ps2dev9.irx`
- `smsutils.irx`
- `ps2ip.irx`
- `ps2smap.irx`
- `smbman.irx`

Internally this is UDPFS over Neutrino's SMAP/ministack path. `smbman.irx` registers the `smb:` ioman device but serves file operations through UDPFS/UDPRDMA.

## Load Order

The expected SMB load order is preserved:

| SMB filename | Internal role |
| --- | --- |
| `poweroff.irx` | Real PS2SDK poweroff handler, including power-button shutdown support. |
| `ps2dev9.irx` | Real DEV9 hardware driver. |
| `smsutils.irx` | Compatibility success module. |
| `ps2ip.irx` | Starts real SMAP hardware driver and ministack, exports `smap` and `mstack`. |
| `ps2smap.irx` | Compatibility success module; SMAP is already active from `ps2ip.irx`. |
| `smbman.irx` | UDPFS ioman driver registered as `smb:`. |

## Layout

- `__poweroff/` builds the real PS2SDK poweroff handler as `poweroff.irx`.
- `__ps2dev9/` builds the real DEV9 dependency as `ps2dev9.irx`.
- `__ps2ip_ministack/` bundles Neutrino SMAP and ministack into `ps2ip.irx`.
- `__smbman_udpfs/` builds the UDPFS ioman driver disguised as `smbman.irx`.
- `__ps2smap_stub/` and `__smsutils_stub/` are compatibility modules that report successful loads.
- `thirdparty/` is ignored and only used locally as source reference material.

## Build

Use a PS2SDK environment:

```sh
make clean package
```

GitHub Actions builds in `ps2dev/ps2dev:latest` and uploads the six IRX files plus `UDPFS_SMB_Spoofer.7z`.
