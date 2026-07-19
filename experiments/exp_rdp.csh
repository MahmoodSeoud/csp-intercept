# exp_rdp.csh - CSH RDP (vmem) integrity experiment. ONE command does everything:
# brings up node 5423, monitors, uploads, reads back, verifies.
#   run /home/mseo/thesis/csp-intercept/experiments/exp_rdp.csh
# Reference "safe" arm: RDP+CRC32 cannot silently corrupt, so verify -> OK is expected.
# Outputs in your csh working directory: rdp_monitor.csv (port 14), rdp_got.bin.
# Region 0x0000AAAAD9BA3F80 = DIPP stora upload scratch (safe, sized for files).
run /home/mseo/thesis/csp-intercept/experiments/bringup_dipp.csh
csp_monitor start -d 14 -m 200 -o rdp_monitor.csv
sleep 1000
upload   -v 2 -n 5423 /home/mseo/thesis/csp-intercept/captures/payload_256k.bin 0x0000AAAAD9BA3F80
download -v 2 -n 5423 0x0000AAAAD9BA3F80 262144 rdp_got.bin
sleep 1000
csp_monitor stop
verify -c /home/mseo/thesis/csp-intercept/captures/payload_256k.manifest rdp_got.bin
