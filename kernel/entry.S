        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # OpenSBI/QEMU 傳進來：
        # a0 = hartid
        # a1 = DTB physical address

        mv s0, a1              # 保存 DTB address

        # set up a stack for C
        la sp, stack0
        li a0, 1024*4
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0

        # start(dtb)
        mv a0, s0
        call start
spin:
        j spin
