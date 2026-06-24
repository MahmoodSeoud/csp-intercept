csp init
csp add can -c can0 -b 0 -d 16
list download 5421
set -n 5421 mng_util_server 5424
set -n 5421 mng_util_interface 0
set -n 5421 mng_util 0
sleep 4000
set -n 5421 mng_util 5426
sleep 6000
ping 5426
