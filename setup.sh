#! /bin/sh
sudo ifconfig lo:0 127.0.0.2 netmask 255.0.0.0 up
sudo ifconfig lo:1 127.0.0.3 netmask 255.0.0.0 up
sudo ifconfig lo:2 127.0.0.4 netmask 255.0.0.0 up
tail -f /var/log/kern.log
