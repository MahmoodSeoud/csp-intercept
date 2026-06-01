# CAN / KISS fault injection (in-path drop shim)

The lossy `zmqproxy` only works on a virtual ZMQ bus: it is an XSUB/XPUB broker you
insert loss into. A real CAN or KISS link is a shared/serial medium with no broker, so
the fault must be injected **in-path** -- on a node's own transmit, before the frame
reaches the wire.

`inject/ci_drop_iface` is that shim: a `csp_iface_t` whose `nexthop` applies the same
deterministic, CSP-aware drop rule as the proxy (keyed by per-flow protocol identity --
RDP seq + wrap epoch, or DTP fragment index -- via the shared `ci_flow_tracker_t` in
`lib/ci_rule`), then delegates kept frames to a real downstream interface. Drop contract:
`csp_buffer_free(packet); return CSP_ERR_NONE;` plus its own `injected_drops` counter
(returning an error would double-free and pollute `tx_error`, corrupting the measurement).

Because both injectors key on the same identity and write the same drop-log schema
(`t_ms,src,dport,csp_flags,is_rdp,index,epoch,dropped`), the proxy drop-log and the shim
drop-log are **interchangeable oracle A** for the two-oracle loop.

## Transport coverage

The monitor APM is transport-agnostic (`csp_route_work` clones every routed packet into
the promisc queue regardless of interface). So:

| Transport | Monitor sees all traffic | Fault injection |
|-----------|--------------------------|-----------------|
| ZMQ | `csp add zmq -p` (promisc; else address-filtered) | `zmqproxy-lossy` broker |
| CAN | promiscuous socketcan (`can_mask=0x0000`) | this shim |
| KISS | always (no address filter) | this shim |

## What is tested in CI

`tests/e2e/drop_iface_host.c` drives the shim's nexthop directly with crafted RDP frames
and a counting stub downstream -- no CAN hardware, no `vcan`, no root. It asserts the
drop contract, leak-freedom, determinism (same seed -> identical drop set), input
partition (every frame dropped XOR delegated), and out-of-scope passthrough. This covers
the full injection LOGIC; what it does not cover is the wiring onto a real CAN interface.

## Real-link demo (manual, needs root for vcan)

NEVER run this against a live hardware CAN bus carrying real traffic. Use a virtual CAN:

```sh
# 1. create a virtual CAN bus (root)
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# 2. in a csh/host node: add the real CAN interface in promiscuous mode, then route
#    outbound traffic through the drop shim that wraps it. (Wiring the shim into csh as
#    a loadable command is future work; today the shim is a library used by a host
#    program -- see drop_iface_host.c for the init pattern:
#      ci_drop_iface_init(&shim, "drop", &can_iface, &rule, mtu, drop_log);
#      csp_rtable_set(dst, mask, &shim.iface, via);   // route via the shim, not can0
#    Kept frames are delegated to can_iface->nexthop; dropped frames are freed in-path.)

# 3. observe with the monitor APM on another node bound to the same vcan0 (promisc),
#    then join its CSV against the shim drop-log exactly like the ZMQ two-oracle loop
#    (scripts/oracle_join.awk).
```

The join math and CSV schemas are identical to the ZMQ path, so `scripts/oracle_join.awk`
and the `two_oracle*.sh` join logic apply unchanged.
