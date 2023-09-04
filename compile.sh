#!/bin/bash

cd /home/femu/share/linux-4.19

sudo make -j8 && sudo make INSTALL_MOD_STRIP=1 modules_install -j8 && sudo make install -j8
