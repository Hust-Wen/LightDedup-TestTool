#!/bin/bash
Share_Dir=$1
sudo mount -t 9p -o trans=virtio hostshare ${Share_Dir} -oversion=9p2000.L
