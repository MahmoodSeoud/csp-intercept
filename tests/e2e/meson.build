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
