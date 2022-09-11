#!/usr/bin/env python3

import mmap
import os

DEV_SYS_MAP_BASE_ADDR = 0xface0000

def devRegRdWrt(action, reg, data=0):
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    mem = mmap.mmap(fd, 4096, offset=DEV_SYS_MAP_BASE_ADDR)
    if( args.action == 'read'):
        reg_data = mem[reg]
        print(f"Register {reg:04x}: {reg_data:02x}")
        print()
    else:
        mem[reg] = data

    mem.close()
    os.close(fd)


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("action", help='Action to perform', type=str, choices=['read', 'write'])
    parser.add_argument("reg", help='Register to read', type=lambda x: int(x,0))  # Handles base 10 or 16 ...
    parser.add_argument("--data", help='Data to write to register', type=lambda x: int(x,0))
    args = parser.parse_args()

    data = 0
    if( args.action == 'write'):
        assert args.data is not None
        data = args.data

    devRegRdWrt(args.action, args.reg, data)

