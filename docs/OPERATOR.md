# Operator guide — checking an upload from csh

You have a file to put on the spacecraft and you want to know it arrived intact.
This page is the exact commands, validated against the real `csh` on the flatsat
(node 5423, `can0`). For building/testing the instrument itself see
[HOWTO.md](HOWTO.md); for the differential experiment see the `scripts/*-sweep`
drivers.

## Two questions, two tools

| You want to know… | Use | Notes |
|---|---|---|
| Did my file arrive intact? | `verify` (integrity oracle) | Any uploader. Never lies. |
| How lossy was the link? | `csp_monitor` on **one** port | Only meaningful for a bulk transfer captured whole. |
| Deployed-vs-fix result | `scripts/{rawdtp,rdp,svu}-sweep` | Uses the injector drop-log + sha256, not the monitor. |

`verify` is the one that matters. It hashes bytes and compares — it does not care
which uploader moved them, so it catches the deployed DTP uploader's silent
corruption that `upload_file` itself will not report.

## The integrity check (round-trip)

Make a manifest of the source, upload, read it back, verify. All four steps are
real and tested:

```sh
# 0. on the ground: pin the source hash
sha256sum file.bin | awk '{print "sha256: "$1}' > file.manifest

# in csh (apm load first, or use an init that loads it):
upload   -v 2 -n 5423 file.bin 0x0000AAAAD9BA3F80          # RDP+CRC32 write
download -v 2 -n 5423 0x0000AAAAD9BA3F80 <SIZE> back.bin   # RDP+CRC32 read-back
verify   -c file.manifest back.bin                          # OK | FAILED
```

`verify` output:

```
back.bin: OK                       # bytes match the manifest
back.bin: FAILED - checksum mismatch   # they do not — the upload is corrupt
```

`verify` also takes `-e <sha256>` instead of `-c <manifest>`, and reports
`verify: cannot read <file>` / `verify: need -c MANIFEST or -e SHA256` on
misuse.

Note on the two uploaders:
- `upload` (vmem, **port 14**, RDP+CRC32) cannot silently corrupt — the transport
  rejects bad packets, so a completed `upload` is already an integrity guarantee.
- `upload_file` (deployed DTP uploader, **port 8**) has **no** end-to-end check and
  will report success over a corrupted file. This is the one `verify` exists to
  catch. Always `verify` after a `upload_file`.

## The loss monitor (optional, bulk transfers only)

```sh
csp_monitor start -d 14        # match ONE port: 8=DTP data, 13=DIPP/RDP, 14=vmem upload
<run the transfer>
csp_monitor stop               # writes csp-intercept.csv
```

Read the rules of the road off the tool's own output:

- The start banner names the port you matched, so a wrong-port capture (which
  logs nothing) is visible immediately.
- **Never `-d -1`** for a loss number. It captures every port, mixes unrelated RDP
  connections, and the tool will (correctly) flag the loss figure untrustworthy.
- `inferred_loss` is only meaningful for a **substantial transfer captured whole**
  (hundreds of contiguous data packets). A tiny transfer is mostly handshake and
  has nothing to measure; the tool prints a WARNING at `stop` and sets
  `suspect_flags & 0x08` on those rows rather than reporting a phantom number.
  When you see that warning, trust `verify`, not the packet count.

## Before any injected run (the stray-injector trap)

A leftover `ci_inject_bridge` on `can0` makes every later transfer stall (the
client hangs after `INIT CAN` with no data). Check first:

```sh
ps -eo pid,args | grep 'tests/e2e/ci_inject_bridge'   # must be empty
```

If one is running, kill it by PID (or `pkill -f 'tests/e2e/ci_inject_bridge'`,
never from a shell whose own command line contains that string). The sweep
drivers now guard on this with `pgrep -f`, not the truncation-broken `pgrep -x`.
