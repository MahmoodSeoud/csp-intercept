# End-to-end tests for the front-ends (Linux/-Dfrontends only). ci_gen produces a
# fixed CSP stream (needs libcsp to frame packets); ci_sub is a discard subscriber
# that keeps the XSUB/XPUB subscription path exercised (needs only libzmq).
e2e_zmq = dependency('libzmq')
e2e_csp = dependency('csp', fallback: ['csp', 'csp_dep'])

ci_gen = executable('ci_gen', 'ci_gen.c',
  dependencies: [e2e_zmq, e2e_csp],
  override_options: ['c_std=gnu11', 'werror=false'])

ci_sub = executable('ci_sub', 'ci_sub.c',
  dependencies: [e2e_zmq],
  override_options: ['c_std=gnu11', 'werror=false'])

# Bound-guard generator: conforming RDP frames interleaved with oversized (>2054B)
# frames that must trip the proxy's recv->memcpy guard instead of overflowing it.
ci_oversize = executable('ci_oversize', 'ci_oversize.c',
  dependencies: [e2e_zmq, e2e_csp],
  override_options: ['c_std=gnu11', 'werror=false'])

# Proves the reproducibility gate: same seed + same stream -> identical drop vector.
# zmqproxy_lossy is the proxy target from proxy/meson.build (same meson scope).
test('proxy determinism (same seed -> identical drop vector)',
  find_program('determinism.sh'),
  args: [zmqproxy_lossy, ci_gen, ci_sub],
  timeout: 90)

# CRITICAL regression: the drop logic must not break XSUB->XPUB subscription
# forwarding (items[1]); otherwise the whole virtual bus silently blackholes.
test('xsub/xpub forwarding regression',
  find_program('forwarding.sh'),
  args: [zmqproxy_lossy, ci_gen, ci_sub],
  timeout: 60)

# Frame-size bound guard (TODOS#4): oversized wire frames must be rejected at the
# recv->memcpy, not overflow the reused packet buffer. Asserts the proxy survives,
# counts them malformed, and keeps forwarding/logging only conforming frames.
test('proxy frame-size bound guard (oversized frame must not overflow)',
  find_program('bound_guard.sh'),
  args: [zmqproxy_lossy, ci_oversize, ci_sub],
  timeout: 90)

# CLI boundary validation: an out-of-range -M (CSP dport 0-63) must be rejected, not
# silently match nothing and produce a falsely-lossless run.
test('proxy -M dport boundary validation',
  find_program('cli_guard.sh'),
  args: [zmqproxy_lossy],
  timeout: 60)

# APM start/stop E2E: stands up a libcsp node and #includes the monitor APM so it
# drives the real start/stop commands + drainer + drain helper headlessly (no csh).
# Guards ISSUE-001 (promisc clones must be drained on stop -> no buffer-pool leak)
# and the per-packet/#WINDOW CSV contract. csp is linked for the node runtime; the
# apm_csh/param includes are header-only (apm.h), so no unresolved dlopen symbols.
# slash is LINKED in full: the APM's start command uses optparse_* (provided by
# slash). The apm/ shared-object can take slash as header-only because the host csh
# resolves optparse at dlopen, but a standalone test exe must link it.
e2e_slash = dependency('slash',   fallback: ['slash', 'slash_dep'])
e2e_apm   = dependency('apm_csh', fallback: ['apm_csh', 'apm_csh_dep']).partial_dependency(includes: true, links: false)
e2e_param = dependency('param',   fallback: ['param', 'param_dep']).partial_dependency(includes: true, links: false)
e2e_threads = dependency('threads')

apm_host = executable('apm_host', 'apm_host.c',
  include_directories: include_directories('../../apm'),
  dependencies: [e2e_csp, e2e_slash, e2e_apm, e2e_param, ci_dep, e2e_threads],
  override_options: ['c_std=gnu11', 'werror=false'])

test('apm start/stop drain (no buffer-pool leak across cycles)', apm_host, timeout: 90)
