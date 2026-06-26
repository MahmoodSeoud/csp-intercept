# Hand-driven demo of the corruption finding

These `.csh` files drive ONLY the CSH side (the receiver / uploader). They do not
inject loss. The loss, and therefore the silent corruption, comes from the
`ci_inject_bridge` process you run in parallel. So:

> **No, `csh -i <file>` alone is not enough.** Without the injector running
> alongside, you just get a clean transfer and learn nothing. The injector IS
> the experiment.

If you just want the result, run the wrapper instead, it does all of this for you:
`scripts/rawdtp-point 0.10 1 mylabel` (look for `result=DELIVERED sha=MISMATCH`).
These files are for when you want to drive each step by hand and watch it.

---

## What each file does

| File | CSH-side role | Notes |
|------|---------------|-------|
| `rawdtp-pull.csh`   | raw libdtp pull, pass 1 (fresh)         | H2 finding. Host-side, no board. |
| `rawdtp-resume.csh` | raw libdtp pull, pass 2+ (`-r` resume)  | Must run in the SAME cwd as pass 1. |
| `dipp-upload.csh`   | deployed dipp `upload_file` (RQ3)       | Needs the ARM `upload_client` on the payload board. |
| `restart-upload-client.csh` | respawn `upload_client` via `mng_util` on node 5421 | Run before each dipp upload (client exits after each transfer). |

CSH address is `-d 16` (us). DTP server is addr `5424`. MTU 256 / throughput 1024.

---

## Infra you need up first (once)

1. **Broker** (lossy ZMQ proxy) on 6000/7000. Check: `ss -ltn | grep -E ':6000|:7000'`.
2. **Loopback DTP server** at addr 5424, started from its own dir so it finds `file.bin`
   (= the pinned 256 KiB payload, sha256 `2c1fa79a…`):
   ```sh
   env -C ~/thesis/disco/src/upload_gs-server ./builddir/upload_gs-server -z 127.0.0.1 -a 5424 &
   ```
   The driver's preflight rejects any `upload_gs-server -c can0`; if one is up and you
   don't need it: `sudo pkill -f 'upload_gs-server -c can0'`.
3. The `libcsh_dtp_client.so` APM at `~/.local/lib/csh/` (these files `apm load -p` from there).

CSH binary: `~/thesis/csh/builddir/csh`.

---

## Run order for the H2 finding (raw DTP)

The false-complete only shows after resume converges to `Missing: 0`, so it takes a few
passes, each with a FRESH injector seed. The receiver writes `dtp_data.bin` +
`dtp_session_meta.bin` into csh's cwd, so pick a working dir and stay in it.

```sh
REPO=~/thesis/csp-intercept
INJ=$REPO/build/tests/e2e/ci_inject_bridge
CSH=~/thesis/csh/builddir/csh
WORK=/tmp/rawdtp-demo; mkdir -p $WORK; cd $WORK     # all state lands here

# ---- PASS 1 (fresh) ----
# terminal A: start the loss source (10% loss, seed 101), then quickly:
$INJ zmq:tcp://127.0.0.1:6000,tcp://127.0.0.1:7000,16 can:can0 8 256 4 0.10 4 101 $WORK/a1.csv 5424 0 &
# terminal B (or right after): drive the receiver
$CSH -i $REPO/csh/demo/rawdtp-pull.csh
# read the "Missing: N bytes" from dtp_info. N > 0 = not done yet.

# ---- PASS 2..N (resume), repeat until dtp_info says Missing: 0 ----
$INJ zmq:tcp://127.0.0.1:6000,tcp://127.0.0.1:7000,16 can:can0 8 256 4 0.10 4 102 $WORK/a2.csv 5424 0 &
$CSH -i $REPO/csh/demo/rawdtp-resume.csh      # SAME cwd ($WORK)
# bump the seed (103, 104, …) each pass. Stop when Missing: 0.
```

Injector args: `<ingress zmq...,rcvaddr=16> <egress can:can0> 8 256 4 <loss> 4 <seed> <droplog.csv> 5424 <pace_us>`.
`pace_us=0` = line rate. Use a real value (426667 for 4800 bit/s at MTU 256) to pace.

### The payoff: when dtp_info finally says `Missing: 0`
That is the transfer reporting itself COMPLETE. Now check the bytes it actually wrote:

```sh
sha256sum $WORK/dtp_data.bin                 # the delivered file
awk '/sha256:/{print $2}' ~/thesis/csp-intercept/captures/payload_256k.manifest   # the truth: 2c1fa79a…
cmp ~/thesis/csp-intercept/captures/payload_256k.bin $WORK/dtp_data.bin            # first differing byte
```

`Missing: 0` (reports delivered) + sha256 that does NOT match = the finding, with your
own eyes. The clean control: do one pass with loss `0` and the sha matches.

---

## RQ3 (deployed dipp uploader) — needs the board

`dipp-upload.csh` drives the real deployed path, but the receiver is the ARM
`upload_client` on the payload node (addr 5426). The client exits after every transfer, so
respawn it first by toggling the `mng_util` param on the A53 (node 5421) THROUGH 0:

```sh
$CSH -i $REPO/csh/demo/restart-upload-client.csh    # set mng_util 0 (kill) -> 5426 (spawn)
# or the bash wrapper that checks the ping for you:
scripts/restart-upload-client 5426 5424
```

`mng_util 0` fires the kill/cleanup callback; `mng_util 5426` fires the spawn. `list download
5421` first so `set -n 5421 mng_util` resolves the param name. Then run the injector on the
`5426` ingress instead of `16`. The `-f` file arg is ignored; the payload is whatever
`file.bin` the server serves. This is the headline 25/25 result, but not solo-on-this-host
like H2.
