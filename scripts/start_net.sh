#!/bin/sh
sudo insmod /usr/rtnet/modules/rtnet.ko
sudo insmod /usr/rtnet/modules/rtipv4.ko
#sudo insmod /usr/rtnet/modules/rtpacket.ko
sudo insmod /usr/rtnet/modules/rtudp.ko
sudo insmod /usr/rtnet/modules/rt_8139too.ko
#sudo insmod /usr/rtnet/modules/rt_loopback.ko
sudo /usr/rtnet/sbin/rtifconfig rteth0 up 192.168.0.1
#sudo /usr/rtnet/sbin/rtifconfig rtlo up 127.0.0.1
sudo /usr/rtnet/sbin/rtroute solicit 192.168.0.2 dev rteth0
