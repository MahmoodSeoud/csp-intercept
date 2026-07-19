# exp_svu.csh - Self-Verifying Uploader integrity experiment, all csh.
# svu_get is the SVU client as a csh command (APM libcsh_svu.so, loaded by apm load).
#   run /home/mseo/thesis/csp-intercept/experiments/exp_svu.csh
# The client block-verifies every block and re-requests what's missing/corrupt, so it
# either writes a VERIFIED file (sha MATCH) or keeps going - it never lands corrupt bytes.
# PREREQ (one-time ground infra): the svu_server binary up on can0 serving payload_256k.bin:
#   cd ~/thesis/csp-intercept && setsid nohup ./build-fe/svu/svu_server \
#       -f captures/payload_256k.bin -c can0 -a 25 >/tmp/svu_srv.log 2>&1 </dev/null &
# Outputs in your csh working directory: svu_monitor.csv (port 9 = SVU data), svu_got.bin.
csp_monitor start -d 9 -m 256 -o svu_monitor.csv
sleep 1000
svu_get -o svu_got.bin -b 4096 -m 256 25
csp_monitor stop
verify -c /home/mseo/thesis/csp-intercept/captures/payload_256k.manifest svu_got.bin
