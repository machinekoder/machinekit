#!/bin/sh
sudo /usr/rtnet/sbin/rtifconfig rteth0 down
#sudo /usr/rtnet/sbin/rtifconfig rtlo down
sudo rmmod rtudp
sudo rmmod rt_8139too
#sudo rmmod rtpacket
#sudo rmmod rt_loopback
sudo rmmod rtipv4
sudo rmmod rtnet
