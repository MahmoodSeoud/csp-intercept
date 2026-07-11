# Self-Verifying Uploader CSP binaries (Approach B scaffold). Standalone client +
# server that join can0 and speak the SVU protocol (svu_proto.h), reusing the proven
# integrity/resume core (ci_dep). gnu11 to match the libcsp headers; not held to
# -Werror yet because these link vendored headers and are still scaffold. The pure
# lib they depend on stays strict.
threads_dep = dependency('threads')
svu_csp_dep = dependency('csp', fallback: ['csp', 'csp_dep'], required: true)

svu_opts = ['c_std=gnu11', 'warning_level=2']
svu_local_inc = include_directories('.')

svu_net_lib = static_library('svu_net', 'svu_net.c',
  dependencies: [svu_csp_dep, threads_dep],
  include_directories: svu_local_inc,
  override_options: svu_opts)

# svu_debug.c is compiled DIRECTLY into each executable (not via the static lib) so
# its strong csp_input_hook/csp_output_hook override libcsp's weak defaults -- a
# static-archive member is only pulled in to resolve an undefined symbol, and the
# weak hooks are already "defined", so an archived override would be silently dropped.
executable('svu_server', ['svu_server.c', 'svu_debug.c'],
  link_with: svu_net_lib,
  dependencies: [svu_csp_dep, threads_dep, ci_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: svu_opts,
  install: false)

executable('svu_client', ['svu_client.c', 'svu_debug.c'],
  link_with: svu_net_lib,
  dependencies: [svu_csp_dep, threads_dep, ci_dep],
  include_directories: [svu_local_inc, ci_inc],
  override_options: svu_opts,
  install: false)
