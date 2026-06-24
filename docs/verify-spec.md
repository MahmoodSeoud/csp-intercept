# `verify` output spec

The verdict an operator sees after an upload. One job: can I trust this file, and
if not, what do I do about it.

## Principles

1. **Copy what engineers already know.** Output mirrors `sha256sum -c` (`name: OK` /
   `name: FAILED`); batch summary mirrors `go test` / `pytest`. Nothing invented.
2. **Quiet on success, loud on failure.** A good file prints one line. A bad file
   prints exactly what broke and the fix. Restraint is the taste.
3. **One honest number.** Loss is always *observed* (measured from what crossed vs
   what was expected). Never "injected." No bench-vs-flight mode. The tool reports
   what it saw, which is true in the lab and in orbit.
4. **Exit code is the contract.** `0` = file trustworthy, `1` = not. Composes into
   any script or CI without reading the text. This is the host CLI (`csp-verify`).
   The in-csh `verify` command prints the same OK/FAILED verdict but always returns
   success — csh has no shell exit code to branch on — so the CLI, not the APM, is
   the exit-code authority for automation.

## Single run — PASS (default)

```
csp_demo.bin: OK
```
exit 0. That's it. Like `sha256sum -c`. The operator trusts it and moves on.

## Single run — FAIL (default)

```
csp_demo.bin: FAILED — reported "delivered" but file is corrupt
  expected   2c1fa79a…628ba5
  received   9b3e0c41…7d0e12
  41 of 1041 fragments missing · 86 KB · 9.7% loss observed
  → re-request: csp_demo.missing.json
```
exit 1.

- Line 1: verdict + the one damning fact (the success signal lied). This is the glance.
- expected/received: the proof, aligned, truncated hashes.
- one line of scale: how much is gone + the observed loss for context.
- the action: where the full missing-fragment list is, so they re-request only those
  (not the whole file — the link is 4800 bit/s).

## Batch / sweep — `go test` style

```
PASS  csp_demo.bin   raw-dtp  0% loss
FAIL  other.bin      raw-dtp  9.7% loss  41 frags missing
2 files: 1 OK, 1 FAILED
```
exit 1 if any FAILED. Verdict token first so the eye catches red instantly. One line
per file so a 30-run sweep stays readable and greppable.

## `-v` (the skeptic / debugging)

Adds the per-check breakdown for someone who wants to argue with the verdict:
- sha256 full hashes
- per-fragment gap detail
- the independent wire-monitor cross-check (observed loss confirmed by a second
  witness), if a monitor capture is present
Default output never shows this. It is opt-in.

## The recovery sidecar

On FAIL, write `<file>.missing.json`: every missing fragment range, machine-readable,
so the operator (or the next pass) feeds it straight back as a selective re-request.
This is the most operationally valuable output after the verdict, because on a
bandwidth-starved link you resend the 86 KB that's missing, not the whole 256 KB.

## Where it runs

A `verify` command emitted inside the csh session right after the upload, so the loop
is `csh -i upload.csh` → verdict on screen. Also available as a `csp-verify` host CLI
for sweeps and CI (same output, same exit codes).

## Inputs

delivered file, the source manifest (pinned sha), and — for the `-v` cross-check only —
the wire-monitor capture if one was taken. No "injected" value appears anywhere in the
product output; the injector is bench plumbing, not something the operator is told about.
