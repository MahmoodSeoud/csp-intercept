# csp-intercept TODOs

## 1. Verify cspdump PCAP timestamping before relying on it as the replay oracle
- **What:** The plan assumes `cspdump` (daniestevez's Rust `csp-tools`) writes
  timestamped PCAP usable for timed replay. It's not vendored here and is currently
  unverified.
- **Why:** The replay / loss-model story depends on timestamped, two-ended capture.
- **Context:** run `cspdump` or read its docs; confirm per-packet timestamps and that
  a two-point capture (XSUB ingress / XPUB egress) is arrangeable.

## 2. v2 - in-path nexthop shim for real UHF/KISS fault injection
- **What:** A custom `csp_iface_t` whose `nexthop` applies `drop_rule` then delegates.
  Drop contract: `csp_buffer_free(packet); return CSP_ERR_NONE;` + keep an own
  injected-drop counter (returning an error code double-frees / pollutes `tx_error`).
- **Why:** The lossy proxy only covers virtual ZMQ experiments; a real UHF/KISS path
  needs an in-path shim. Deferred from v1.
- **Depends on:** confirming whether the real ground link is KISS or ZMQ-bridged.

## 3. Contributions 2/3 - reuse the existing DTP loss oracle
- **What:** The team's DTP tooling already tracks received vs missing segments per
  session. Reuse that as an independent DTP-loss ground truth rather than parsing DTP
  fragments off the wire.
- **Why:** Free cross-check for the proxy's drop-vector and the cheapest DTP-loss
  signal for the RDP-vs-DTP analysis and the uploader comparison.
- **Depends on:** v1 instrument working first.

> Local-only notes (DTP internals, a reliability issue to raise with the team, and
> the full review findings) are kept out of this repo - see NOTES.local.md and the
> design doc under ~/.gstack/projects/csp-intercept/.
