#!/bin/bash.sh
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash -z 0x310000 build/storage.bin 
