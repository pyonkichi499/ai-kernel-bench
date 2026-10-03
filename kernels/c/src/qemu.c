/* QEMU の isa-debug-exit デバイスによる終了（仕様は kui/qemu.h を参照） */
#include <kui/arch/x86_64/cpu.h>
#include <kui/arch/x86_64/io.h>
#include <kui/qemu.h>

#define QEMU_DEBUG_EXIT_PORT 0xf4

_Noreturn void qemu_exit(enum qemu_exit_code code)
{
	outb(QEMU_DEBUG_EXIT_PORT, (uint8_t)code);
	/*
	 * isa-debug-exit がない環境（実機など）では何も起きずにここへ来るので、止まるしかない。
	 * なお QEMU でも、終了処理が終わるまで CPU がわずかに先へ進むことがある。
	 * ここで何かを出力すると、その途中までがログに混ざるので、何も出さずに止まる。
	 */
	cpu_halt_forever();
}
