# exp_dtp_pull.csh - Deployed DTP (dtp_client PULL) integrity experiment. ONE command:
# monitors, pulls the file, verifies.
#   run /home/mseo/thesis/csp-intercept/experiments/exp_dtp_pull.csh
# This is the arm that reports success on a corrupt file under loss: verify -> FAILED
# when the link drops fragments, "delivered" regardless.
# PREREQ (one-time ground infra): the gs-server up on can0 at 5424 serving payload_256k.bin
# as its file.bin (so verify matches the manifest below).
# Outputs in your csh working directory: dtp_monitor.csv (port 8), dtp_data.bin.
csp_monitor start -d 8 -m 256 -O 4 -o dtp_monitor.csv
sleep 1000
dtp_client -n 5424 -i 0 -m 256 -t 1024
sleep 3000
dtp_info
csp_monitor stop
verify -c /home/mseo/thesis/csp-intercept/captures/payload_256k.manifest dtp_data.bin
