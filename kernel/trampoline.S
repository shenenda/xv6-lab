	#
        # 在用户态与内核态之间切换的代码。
        #
        # 这段代码在用户页表和内核页表中都映射到同一虚拟地址 TRAMPOLINE，
        # 因而切换页表前后可以连续执行。
	#
	# kernel.ld 保证本段起始地址按页对齐。
        #
	.section trampsec
.globl trampoline
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c 让 stvec 指向这里，因此用户态陷阱会从 uservec 开始；
        # 此时已进入监管者模式，但仍使用用户页表。
        #
        # sscratch 保存 trapframe 在用户地址空间中的固定映射地址 TRAPFRAME。
        #
        
	# 交换 a0 与 sscratch，使 a0 得到 TRAPFRAME，同时暂存原用户 a0。
        csrrw a0, sscratch, a0

        # 把用户寄存器依照 trapframe 字段偏移保存到 TRAPFRAME。
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# 把此前暂存在 sscratch 中的用户 a0 保存到 p->trapframe->a0。
        csrr t0, sscratch
        sd t0, 112(a0)

        # 从 p->trapframe->kernel_sp 装入内核栈指针。
        ld sp, 8(a0)

        # 从 p->trapframe->kernel_hartid 恢复 tp，使其保存当前 hart 编号。
        ld tp, 32(a0)

        # 从 p->trapframe->kernel_trap 取出 usertrap() 的入口地址。
        ld t0, 16(a0)

        # 从 p->trapframe->kernel_satp 取出内核页表的 satp 值。
        ld t1, 0(a0)
        csrw satp, t1
        sfence.vma zero, zero

        # 切到内核页表后，a0 中的 TRAPFRAME 用户虚拟地址不再有效，
        # 因为内核页表没有对 p->trapframe 做相同的固定地址映射。

        # 跳转到不会直接返回的 usertrap()。
        jr t0

.globl userret
userret:
        # userret(TRAPFRAME, pagetable) 由 usertrapret() 调用，负责从内核返回用户态。
        # a0 是用户页表中的 TRAPFRAME 地址，a1 是将写入 satp 的用户页表值。

        # 写 satp 并刷新 TLB，切换到用户页表。
        csrw satp, a1
        sfence.vma zero, zero

        # 先把保存的用户 a0 放入 sscratch，最后再与当前保存 TRAPFRAME 的 a0 交换。
        ld t0, 112(a0)
        csrw sscratch, t0

        # 从 TRAPFRAME 恢复除 a0 外的所有用户寄存器。
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# 恢复用户 a0，并把 TRAPFRAME 地址留在 sscratch 供下次陷阱使用。
        csrrw a0, sscratch, a0
        
        # usertrapret() 已设置好 sstatus 和 sepc；sret 返回用户模式及原用户 pc。
        sret
