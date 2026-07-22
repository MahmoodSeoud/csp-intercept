# experiment.csh - SVU APM smoke test, run INSIDE a csh already on can0.
#
# This exercises the DEVELOPER-FACING path: the svu_get slash command loaded into a
# live csh (which already did csp_init + the CAN interface). The server is a separate
# binary on another node, so start it from a normal shell FIRST:
#
#     head -c 65536 /dev/urandom > /tmp/svu_in.bin
#     ./build-fe/svu/svu_server -c can0 -a 25 -f /tmp/svu_in.bin -b 4096 &
#
# Then run this script:   ./build-fe/csh -i svu/experiment.csh
# (adjust -p to wherever libcsh_svu.so lives; the loader requires the libcsh_ prefix)
#
# After it exits, verify in a shell that the pull matched the source:
#     sha256sum /tmp/svu_in.bin /tmp/svu_apm_out.bin      # the two hashes must match

# load the SVU APM -> registers the `svu_get` command
apm load -f libcsh_svu.so -p /home/mseo/thesis/csp-intercept/build-fe/svu

# pull the file node 25 is serving, block-verify it, write it out.
# svu_get prints "VERIFIED <n> bytes in <r> round(s)" on success.
svu_get 25 -o /tmp/svu_apm_out.bin -b 4096 -m 200

# a pty-run csh needs a final exit or it hangs (no EOF); harmless interactively.
exit
