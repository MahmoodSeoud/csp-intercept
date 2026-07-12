# Self-Verifying Uploader CSP binaries (Approach B scaffold). Standalone client +
# server that join can0 and speak the SVU protocol (svu_proto.h), reusing the proven
# integrity/resume core (ci_dep). gnu11 to match the libcsp headers; not held to
# -Werror yet because these link vendored headers and are still scaffold. The pure
# lib they depend on stays strict.
threads_dep = dependency('threads')
svu_csp_dep = dependency('csp', fallback: ['csp', 'csp_dep'], required: true)
# libzmq: svu_net's -z path uses csp_zmqhub so the server can sit behind the loss
# injector. Only needed at link time for that symbol; CAN-only use never calls it.
svu_zmq_dep = dependency('libzmq', required: true)

svu_opts = ['c_std=gnu11', 'warning_level=2']
svu_local_inc = include_directories('.')

svu_net_lib = static_library('svu_net', 'svu_net.c',
  dependencies: [svu_csp_dep, threads_dep, svu_zmq_dep],
  include_directories: svu_local_inc,
  override_options: svu_opts)

# The transfer loop, decoupled from CSP bring-up so BOTH the standalone client and
# the CSH APM share it. Does not csp_init -- the caller owns the stack.
svu_session_lib = static_library('svu_session', 'svu_session.c',
  dependencies: [svu_csp_dep, ci_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: svu_opts)

# svu_debug.c is compiled DIRECTLY into each executable (not via the static lib) so
# its strong csp_input_hook/csp_output_hook override libcsp's weak defaults -- a
# static-archive member is only pulled in to resolve an undefined symbol, and the
# weak hooks are already "defined", so an archived override would be silently dropped.
executable('svu_server', ['svu_server.c', 'svu_debug.c'],
  link_with: svu_net_lib,
  dependencies: [svu_csp_dep, threads_dep, ci_dep, svu_zmq_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: svu_opts,
  install: false)

executable('svu_client', ['svu_client.c', 'svu_debug.c'],
  link_with: [svu_net_lib, svu_session_lib],
  dependencies: [svu_csp_dep, threads_dep, ci_dep, svu_zmq_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: svu_opts,
  install: false)

# CSH APM: `svu_get` slash command. Loads into a live csh (which already did
# csp_init + CAN), so it links svu_session (NOT svu_net -- must not re-init CSP).
# Dependency set + b_lundef=false mirror apm/csp_monitor_apm.c (a plugin has
# undefined symbols the host csh resolves at dlopen).
apm_csh_dep   = dependency('apm_csh', fallback: ['apm_csh', 'apm_csh_dep'], required: true)
apm_slash_dep = dependency('slash',   fallback: ['slash', 'slash_dep'], required: false).partial_dependency(links: false, includes: true)
apm_param_dep = dependency('param',   fallback: ['param', 'param_dep']).as_link_whole()

shared_library('csh_svu', 'svu_apm.c',
  link_with: svu_session_lib,
  dependencies: [apm_csh_dep, apm_slash_dep, apm_param_dep, svu_csp_dep, ci_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true', 'b_lundef=false'],
  install: true)
