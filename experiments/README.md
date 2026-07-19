# Upload integrity experiments (csh)

Each experiment is a single `.csh` you `run` from your csh session. It does the whole
thing — brings up the node it needs, starts the monitor, uploads, stops the monitor,
verifies — and writes its own monitor CSV and its own received file, so the arms are
directly comparable. No shell scripts.

Run from an initialised session (`csp init` + `csp add can` + `apm load` already done):

```
run /home/mseo/thesis/csp-intercept/experiments/exp_rdp.csh
run /home/mseo/thesis/csp-intercept/experiments/exp_dtp_pull.csh
run /home/mseo/thesis/csp-intercept/experiments/exp_upload_file.csh
run /home/mseo/thesis/csp-intercept/experiments/exp_svu.csh
```

| Arm | File | Uploader | Monitor port | Verify |
|---|---|---|---|---|
| CSH RDP (reference) | `exp_rdp.csh` | `upload`/`download` (vmem) | 14 | expect **OK** |
| Deployed DTP (pull) | `exp_dtp_pull.csh` | `dtp_client` | 8 | **FAILED** under loss |
| Deployed DTP (push) | `exp_upload_file.csh` | `upload_file` | 8 | none (lands on payload) |
| SVU | `exp_svu.csh` | `svu_get` (csh command) | 9 | expect **VERIFIED/OK** |

`svu_get` is the SVU client as a native csh command (APM `libcsh_svu.so`, loaded by
`apm load`). Its server is a one-time ground binary like the gs-server (see below).

`verify` is the proof (did the file arrive intact); the monitor CSV is context (how
lossy the link was). The monitor number is trustworthy only for a bulk transfer
captured whole — on RDP it double-counts retransmits, so treat RDP's figure as
indicative; RDP's real result is `verify → OK`.

## Node bring-up (also csh, chained via `run`)

The experiments pull these in automatically, but you can run them alone too:

- `bringup_dipp.csh` — turns on the DIPP node (5423) via `mng_dipp` on the A53 manager
  (5421). `exp_rdp.csh` runs this first.
- `bringup_upload_client.csh` — turns on the Upload-Client (5426) via `mng_util` on 5421.
  `exp_upload_file.csh` runs this first.

## One-time ground infra (not csh — a server binary, started once)

The two DTP arms pull from the ground gs-server. Start it once per session:

```
cd ~/thesis/disco/src/upload_gs-server/builddir
cp ~/thesis/csp-intercept/captures/payload_256k.bin file.bin
setsid nohup ./upload_gs-server -c can0 -a 5424 >/tmp/upsrv_can0.log 2>&1 </dev/null &
ps -eo pid,args | grep '[u]pload_gs-server'    # liveness check — NOT ping (the server ignores ping)
```

Its `file.bin` must be the same bytes as `payload_256k.bin`, or the pull arms' `verify`
compares against the wrong manifest and reports a false FAILED.

## Loss

These run on live can0, so loss is only what the real link drops. To sweep controlled
loss levels use the host injector bench (`scripts/*-sweep`).
