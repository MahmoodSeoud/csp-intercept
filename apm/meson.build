# Monitor APM: a CSH-loadable shared object. The loader picks up libcsh_*.so, so
# this target name produces libcsh_csp_monitor.so. Dependency set mirrors dipp-apm:
# apm_csh provides apm_init_version + the slash-command section collector (its dep
# is already declared as_link_whole, so we do not re-apply it); param/slash/csp are
# the CSH runtime; ci is our shared parse/rule/measurement lib.
apm_csh_dep   = dependency('apm_csh', fallback: ['apm_csh', 'apm_csh_dep'], required: true)
apm_csp_dep   = dependency('csp',     fallback: ['csp', 'csp_dep'], required: true)
apm_slash_dep = dependency('slash',   fallback: ['slash', 'slash_dep'], required: false).partial_dependency(links: false, includes: true)
apm_param_dep = dependency('param',   fallback: ['param', 'param_dep']).as_link_whole()

shared_library('csh_csp_monitor',
  ['csp_monitor_apm.c'],
  dependencies: [apm_csh_dep, apm_slash_dep, apm_param_dep, apm_csp_dep, ci_dep],
  # gnu11 to match the CSH headers; our own APM code is held to -Werror.
  # b_lundef=false: an APM is a plugin -- it legitimately has undefined symbols
  # (e.g. vmem_add) that the host csh process resolves at dlopen. dipp-apm does the
  # same. Without this the .so won't link.
  override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true', 'b_lundef=false'],
  install: true)
