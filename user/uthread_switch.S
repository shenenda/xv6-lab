	.text

	/*
         * 保存旧线程的寄存器，并恢复新线程的寄存器。
         * 至少要保留 ra、sp 以及 RISC-V 调用约定中的被调用者保存寄存器。
         */

	.globl thread_switch
thread_switch:
	/* 在此补充线程上下文保存与恢复汇编。 */
	ret    /* 跳转到恢复后的 ra；新线程首次运行时 ra 应指向线程入口函数。 */
