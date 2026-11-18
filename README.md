# TINY64-OS

Install Qemu on system:
```sh
sudo apt install qemu-system
```

On Windows apply a E1000 network card, and enable this in powershell for virtualbox DNS.
```ps
$vb = "$env:ProgramFiles\Oracle\VirtualBox\VBoxManage.exe"
& $vb modifyvm "floppu" --natdnsproxy1 on
```

To rebuild everything from scratch:

```sh
make clean && make && make run QEMU='qemu-system-x86_64 -cpu max -machine pc,vmport=on'
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

## References
<a href="https://wiki.osdev.org/Setting_Up_Long_Mode#Entering_Long_Mode" target="_blank">Entering Long Mode (64-bit)</a><br/>
<a href="https://github.com/TheOSDevLab/Crafting-an-OS-Notes-and-Insights/blob/main/roadmap/03_bootloader_development.md#chs-second-stage-project" target="_blank">Basics of Bootloading</a><br/>
<a href="https://wiki.osdev.org/Expanded_Main_Page" target="_blank">Main Wiki OSDev</a><br/>
<a href="https://github.com/torvalds/linux/blob/master/arch/x86/boot/compressed/head_64.S" target="_blank">Linux Bootloader ASM</a><br/>
<a href="https://github.com/torvalds/linux/blob/master/arch/x86/kernel/head64.c" target="_blank">Linux Start.c</a><br/>
<a href="https://github.com/torvalds/linux/blob/master/init/main.c" target="_blank">Linux kernel.c</a>

