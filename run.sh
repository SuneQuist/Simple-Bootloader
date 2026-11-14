#!/bin/bash

make clean && make
make run QEMU='qemu-system-x86_64 -cpu max -machine pc,vmport=on'
