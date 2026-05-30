project('csp-intercept', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=true'])

# Shared parse/rule library (Eng review decision 4: one source of truth, linked
# by both the monitor APM and the lossy proxy). Pure C, no libcsp dependency in
# the parse layer, so it is trivially unit-testable.
ci_inc = include_directories('lib')
ci_lib = static_library('ci',
  ['lib/ci_rdp.c', 'lib/ci_dtp.c', 'lib/ci_rule.c', 'lib/ci_meas.c'],
  include_directories: ci_inc,
  install: false)

ci_dep = declare_dependency(link_with: ci_lib, include_directories: ci_inc)

# Unit tests (dependency-free).
test_lib = executable('test_lib', 'tests/test_lib.c', dependencies: ci_dep)
test('lib unit tests', test_lib)

# apm/ (monitor) and proxy/ (vendored lossy zmqproxy) are added once libcsp/CSH
# submodules are vendored -- see TODOS.md and the design doc Next Steps.
