csp init
csp add can -c can0 -b 0 -d 16
apm load -p /home/mseo/.local/lib/csh
upload_file -f /home/mseo/thesis/csp-intercept/captures/payload_256k.bin -d /home/root/csp_demo.bin -n 5426 -s 5424
sleep 30000
