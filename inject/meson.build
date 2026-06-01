# In-path drop shim: a csp_iface_t that applies the deterministic CSP-aware drop rule
# on a node's own TX before it hits a real link (CAN/KISS/...), where the ZMQ proxy
# cannot reach. Transport-neutral: it wraps any downstream csp_iface_t's nexthop.
# Links libcsp (csp_buffer_free, csp_get_ms) + our shared ci rule/parse lib.
inject_csp_dep = dependency('csp', fallback: ['csp', 'csp_dep'])

ci_drop_inc = include_directories('.')
ci_drop_lib = static_library('ci_drop_iface',
  ['ci_drop_iface.c'],
  include_directories: ci_drop_inc,
  dependencies: [inject_csp_dep, ci_dep],
  # our own code, held to the project strict standard
  override_options: ['c_std=c11', 'warning_level=2', 'werror=true'],
  install: false)

ci_drop_dep = declare_dependency(
  link_with: ci_drop_lib,
  include_directories: ci_drop_inc,
  dependencies: [inject_csp_dep, ci_dep])
