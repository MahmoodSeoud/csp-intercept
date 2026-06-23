# Case study: silent upload corruption on DISCO-2

**One line:** DISCO-2's file-upload software reports "File uploaded." while delivering a
corrupted file under realistic link loss. Measured on a flatsat replica, paced to the real
9.6 kbit/s uplink, confirmed by an independent SHA-256 oracle.

*Prepared by {your name}, DISCO team / Aarhus University. Audit tool: satguard.*

---

## What we found

Under injected packet loss at rates spanning plausible UHF link conditions (1-30%), paced to the
real 9.6 kbit/s uplink rate, the deployed upload path delivered a file that was the **right size but
the wrong bytes**, and the client logged success every time.
Nothing in the flight or ground software flagged it. The only thing that caught it was an
external checksum that the upload path itself never runs.

| Upload mechanism | Under loss | Verdict |
|---|---|---|
| Deployed upload client (fire-and-forget) | 25/25 delivered corrupt, reported success | **VULNERABLE** |
| Raw DTP resume (libdtp native) | 8/8 reported DELIVERED, failed checksum | **VULNERABLE** |
| Recovery off (control) | 15/15 never complete | incomplete (expected) |
| SHA-256 verify + retry | completes with verified integrity | **OK** |

Clean control: at **0% loss, 0/5** transfers corrupted. The corruption is caused by link loss,
not by the measurement rig. That clean control is the proof.

## Where the bug lives (it's in our own code)

This is not a library bug we inherited. It is in DISCO-2's own integration:

`disco/src/upload_sat-client/src/session/session_hooks.c` → `apm_on_end()`

On end-of-transfer the hook closes the received segments, computes which fragments are missing,
and then simply finishes. It never verifies the file against the original and never fails on a
gap. Dropped fragments stay as zero-holes inside a right-size file that is accepted as complete.
The libcsp CRC32 primitives exist (`csp_crc32_verify`), they're just never applied end-to-end on
the upload path.

A second, independent failure: libdtp's cross-session resume (the obvious "fix") sets the session
total from an uninitialised field, truncates the session, abandons the high-index gaps, and then
declares `Missing: 0`. It also reports success while delivering corruption. The fix everyone reaches
for is itself broken under loss.

## Why it matters

A file you upload to a satellite can't be checked by hand once it's up there. If the bytes are
corrupt and the software says "done," you find out when the spacecraft misbehaves, and by then it's
unreachable. **PicSat** (Paris Observatory, 3U CubeSat) was lost in 2018 after a software upload
arrived corrupted and the onboard software didn't catch it. Right-size, wrong-bytes, accepted as
uploaded, is a satellite-loss risk, not a cosmetic one.

## The fix

An end-to-end SHA-256 verify-and-retry on the upload path turns *reported* completion into
*verified* completion. In our measurements it is the only mechanism that delivers correctly under
loss (it completes at 0-30% loss for roughly 1.0x-1.7x bytes). The fix is cheap. The expensive part
is not knowing you need it.

## How this was measured (so you can trust it)

- Deterministic, CSP-aware packet loss injected per fragment at swept rates (1/5/10/20/30%).
- Traffic paced to the real 9.6 kbit/s UHF uplink rate.
- Delivery verified by an independent SHA-256 oracle against the original file.
- Zero-loss control run alongside every sweep to prove corruption is loss-caused.
- Run on a flatsat hardware replica (disclosed: ground replica, not the on-orbit unit).

Full per-run data and the audit report (`disco2_audit.html`) accompany this note.

---

## What I'm asking DISCO for

1. **Confirm the finding** against your understanding of the upload path (you know it better than anyone).
2. **Let me name DISCO as the first reference** for this audit: a one-line quote and permission to
   show this case study (sanitised as you wish) to other cubesat teams.
3. **One or two intros** to people you know on other university or commercial cubesat teams who push
   files to a satellite, so I can run the same free audit for them.

> _"<placeholder for a one-line quote from a DISCO lead — e.g. 'We didn't know our upload path could
> accept a corrupt file and report success. This audit showed it on our own hardware.'>"_
> — {name, role, DISCO / Aarhus University}
