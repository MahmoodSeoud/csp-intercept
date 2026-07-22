# exp_dtp_pull.csh - RETIRED (2026-07-22).
#
# This arm used `dtp_client` (libcsh_dtp_client.so) to PULL a file over DTP. But dtp_client
# is the DIPP *downlink* toolset (it ships with dtp_info/ring_size/observation_meta) — a
# ground PROXY for the libdtp client, NOT the deployed DTP *upload* software. It happens to
# call the same dtp_client_main(), so it shows the same silent corruption, but it is not the
# real deployed path.
#
# The real DTP arm is the deployed upload_gs-server + upload_client pair, run on the payload
# board: see exp_upload_file.csh. Use that as the DTP result; this file is kept only as a
# pointer. (Old driver: scripts/rawdtp-point; data: captures/rawdtp_sweep.csv.)
