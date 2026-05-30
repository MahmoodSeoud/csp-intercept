# Lossy zmqproxy: vendored from CSH (~/projects/csh/src/zmqproxy.c) and built with
# -DZMQ_PROXY_LOSSY so the seeded drop/delay path compiles in. Links the full
# libcsp (as_link_whole, matching CSH) + system libzmq + our shared ci lib.
#
# werror is off for this target: the base is vendored third-party code; only the
# logic we add on top is held to the project's -Werror standard (reviewed by eye).
zmq_dep = dependency('libzmq')
# Plain link: the proxy references the csp symbols it uses (csp_id_strip, csp_get_ms,
# csp_queue_*), so the linker pulls them. Whole-archive is only needed for the APM's
# slash-command section, not here.
proxy_csp_dep = dependency('csp', fallback: ['csp', 'csp_dep'])

zmqproxy_lossy = executable('zmqproxy-lossy',
  ['zmqproxy.c'],
  c_args: ['-DZMQ_PROXY_LOSSY'],
  dependencies: [zmq_dep, proxy_csp_dep, ci_dep],
  # gnu11 for the vendored base (usleep / _DEFAULT_SOURCE); werror off because the
  # base is third-party. The loss-rewrite we add on top is reviewed by eye.
  override_options: ['c_std=gnu11', 'werror=false'],
  install: true)
