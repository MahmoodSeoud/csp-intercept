csp init
csp add can -c can0 -b 0 -d 16
apm load -p /home/mseo/.local/lib/csh
dtp_client -n 5424 -i 0 -m 256 -t 1024 -r
sleep 3000
dtp_info
