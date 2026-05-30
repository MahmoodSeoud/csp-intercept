# csp-intercept TODOs

## 1. Verify cspdump PCAP timestamping before relying on it as the replay oracle
- **What:** The plan assumes `cspdump` (daniestevez's Rust `csp-tools`) writes
  timestamped PCAP usable for timed replay. It's not vendored here and is currently
  unverified.
- **Why:** The replay / loss-model story depends on timestamped, two-ended capture.
- **Context:** run `cspdump` or read its docs; confirm per-packet timestamps and that
  a two-point capture (XSUB ingress / XPUB egress) is arrangeable.
- **Blocking (eng review 2026-05-30):** verify this BEFORE building the proxy
  determinism/replay machinery, not after. The replay-vector path and per-flow-identity
  alignment depend on a reconstructable capture; building the machinery around a capture
  you can't produce is wasted work.

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

## 4. Proxy frame-size bound guard (accepted-risk trip-wire from eng review 2026-05-30)
- **What:** Add an upper-bound check `datalen <= sizeof(packet->data)` at every
  `recv -> memcpy(frame_begin, zmq_data, datalen)` site in the vendored proxy
  (`task_capture`, the hdx block, and the new CSP-aware match block); on violation
  drop the frame and increment a malformed-frame counter.
- **Why:** v1 accepts the risk because a closed virtual testbed only carries
  conforming zmqhub frames (bounded by `buffer_size`). It becomes a live heap-overflow
  the moment an oversized/non-conforming frame appears.
- **Trigger (do this BEFORE):** first real-pass replay, attaching an external publisher,
  or pointing `tc netem` / any non-CSH producer at the proxy.

## 5. DTP-completion measurement confound (contributions 2/3, eng review 2026-05-30)
- **What:** Document (or fix upstream) that lossy DTP transfers never cleanly complete:
  `dtp_client.c:121-130` completion test is `nof_csp_packets >= expected` over RECEIVED
  packets, so under ANY injected port-8 loss the client only ever exits via idle-timeout
  with a partial payload.
- **Why:** Every DTP "completion" in the exact lossy regime this instrument creates is a
  timeout artifact, so any DTP throughput / latency-to-complete metric is confounded.
  Must be accounted for (or the client fixed) before publishing RDP-vs-DTP numbers.
- **Depends on:** the bug is in the team's `dtp_client` (separate repo); see NOTES.local.md.

> Local-only notes (DTP internals, a reliability issue to raise with the team, and
> the full review findings) are kept out of this repo - see NOTES.local.md and the
> design doc under ~/.gstack/projects/csp-intercept/.
