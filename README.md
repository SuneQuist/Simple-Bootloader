# Simple-Bootloader

To build and run:

```sh
make && make run
```

To rebuild everything from scratch:

```sh
make clean && make && make run
```

## Boot sequence

```text
QEMU (legacy BIOS)
    |
    v
boot.s (16-bit)
    | Loads kernel at 0x8000
    v
start.s
    | Enables A20
    | Enters 32-bit protected mode
    | Creates page tables
    | Enables 64-bit long mode
    | Sets stack pointer
    v
kernel_main() in C
    |
    | Writes "Hello from 64-bit C!"
    | to VGA text memory
    v
CPU halts
```

### References
<a href="https://wiki.osdev.org/Setting_Up_Long_Mode#Entering_Long_Mode" target="_blank">Entering Long Mode (64-bit)</a>
