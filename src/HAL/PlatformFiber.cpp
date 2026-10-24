#include "HAL/PlatformFiber.h"

#include <cstring>

#if FP_PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

#if !FP_PLATFORM_WINDOWS
	#if defined(__APPLE__)
		#define FP_ASM_SYMBOL(name) "_" #name
	#else
		#define FP_ASM_SYMBOL(name) #name
	#endif

// fp_fiber_switch(void** saveStack, void* loadStack) pushes the callee saved registers, stores the
// stack pointer through saveStack, loads loadStack and pops the registers that fiber saved.
// fp_fiber_start is where a new fiber first lands. It calls entry(arg) taken from saved registers.
extern "C" void fp_fiber_switch(void** saveStack, void* loadStack);
extern "C" void fp_fiber_start();

	#if defined(__aarch64__)
// AAPCS64 callee saved: x19 to x30 and d8 to d15. The frame is 160 bytes, keeping sp 16 byte aligned.
__asm__(
	".text\n"
	".p2align 4\n"
	".globl " FP_ASM_SYMBOL(fp_fiber_switch) "\n"
	FP_ASM_SYMBOL(fp_fiber_switch) ":\n"
	"	sub sp, sp, #160\n"
	"	stp x19, x20, [sp, #0]\n"
	"	stp x21, x22, [sp, #16]\n"
	"	stp x23, x24, [sp, #32]\n"
	"	stp x25, x26, [sp, #48]\n"
	"	stp x27, x28, [sp, #64]\n"
	"	stp x29, x30, [sp, #80]\n"
	"	stp d8, d9, [sp, #96]\n"
	"	stp d10, d11, [sp, #112]\n"
	"	stp d12, d13, [sp, #128]\n"
	"	stp d14, d15, [sp, #144]\n"
	"	mov x9, sp\n"
	"	str x9, [x0]\n"
	"	mov sp, x1\n"
	"	ldp x19, x20, [sp, #0]\n"
	"	ldp x21, x22, [sp, #16]\n"
	"	ldp x23, x24, [sp, #32]\n"
	"	ldp x25, x26, [sp, #48]\n"
	"	ldp x27, x28, [sp, #64]\n"
	"	ldp x29, x30, [sp, #80]\n"
	"	ldp d8, d9, [sp, #96]\n"
	"	ldp d10, d11, [sp, #112]\n"
	"	ldp d12, d13, [sp, #128]\n"
	"	ldp d14, d15, [sp, #144]\n"
	"	add sp, sp, #160\n"
	"	ret\n"
	".globl " FP_ASM_SYMBOL(fp_fiber_start) "\n"
	FP_ASM_SYMBOL(fp_fiber_start) ":\n"
	"	mov x0, x20\n"
	"	blr x19\n"
	"	brk #0\n");

namespace
{
	constexpr FP::usize FrameSize = 160;

	// Lay out a saved frame so the first switch pops entry into x19, arg into x20 and returns into fp_fiber_start.
	void* PrepareStack(void* top, FP::PlatformFiber::EntryFn entry, void* arg)
	{
		auto* frame = reinterpret_cast<FP::uint64*>(static_cast<FP::uint8*>(top) - FrameSize);
		std::memset(frame, 0, FrameSize);
		frame[0] = reinterpret_cast<FP::uint64>(entry); // x19
		frame[1] = reinterpret_cast<FP::uint64>(arg);   // x20
		frame[11] = reinterpret_cast<FP::uint64>(&fp_fiber_start); // x30
		return frame;
	}
}

	#elif defined(__x86_64__)
// System V callee saved: rbx, rbp, r12 to r15, plus the SSE and x87 control words.
__asm__(
	".text\n"
	".p2align 4\n"
	".globl " FP_ASM_SYMBOL(fp_fiber_switch) "\n"
	FP_ASM_SYMBOL(fp_fiber_switch) ":\n"
	"	pushq %rbp\n"
	"	pushq %rbx\n"
	"	pushq %r12\n"
	"	pushq %r13\n"
	"	pushq %r14\n"
	"	pushq %r15\n"
	"	subq $8, %rsp\n"
	"	stmxcsr (%rsp)\n"
	"	fnstcw 4(%rsp)\n"
	"	movq %rsp, (%rdi)\n"
	"	movq %rsi, %rsp\n"
	"	ldmxcsr (%rsp)\n"
	"	fldcw 4(%rsp)\n"
	"	addq $8, %rsp\n"
	"	popq %r15\n"
	"	popq %r14\n"
	"	popq %r13\n"
	"	popq %r12\n"
	"	popq %rbx\n"
	"	popq %rbp\n"
	"	ret\n"
	".globl " FP_ASM_SYMBOL(fp_fiber_start) "\n"
	FP_ASM_SYMBOL(fp_fiber_start) ":\n"
	"	movq %r13, %rdi\n"
	"	callq *%r12\n"
	"	ud2\n");

namespace
{
	// Control words, six registers and the return address. The ret leaves rsp 16 byte aligned,
	// so the call in fp_fiber_start enters entry with the alignment the ABI expects.
	void* PrepareStack(void* top, FP::PlatformFiber::EntryFn entry, void* arg)
	{
		auto* slots = reinterpret_cast<FP::uint64*>(static_cast<FP::uint8*>(top) - 16) - 8;
		std::memset(slots, 0, sizeof(FP::uint64) * 8);
		slots[0] = 0x037F'0000'1F80ull; // mxcsr default in the low half, x87 control word above it
		slots[1] = 0;                                         // r15
		slots[2] = 0;                                         // r14
		slots[3] = reinterpret_cast<FP::uint64>(arg);         // r13
		slots[4] = reinterpret_cast<FP::uint64>(entry);       // r12
		slots[5] = 0;                                         // rbx
		slots[6] = 0;                                         // rbp
		slots[7] = reinterpret_cast<FP::uint64>(&fp_fiber_start); // return address
		return slots;
	}
}
	#else
		#error "Fibers need a context switch for this CPU"
	#endif
#endif

namespace FP::PlatformFibers
{
	bool Create(PlatformFiber& fiber, usize stackSize, PlatformFiber::EntryFn entry, void* arg)
	{
		const usize page = PlatformMemory::GetPageSize();
		stackSize = PlatformMemory::RoundUpToPage(stackSize < page ? page : stackSize);
#if FP_PLATFORM_WINDOWS
		// Windows manages fiber stacks itself, guard page included.
		fiber.Context = CreateFiberEx(stackSize, stackSize, 0, reinterpret_cast<LPFIBER_START_ROUTINE>(entry), arg);
		fiber.StackSize = stackSize;
		fiber.OwnsStack = true;
		return fiber.Context != nullptr;
#else
		// Reserve one extra page and leave the lowest one uncommitted as the guard.
		MemoryBlock range = PlatformMemory::Reserve(stackSize + page);
		if (!range || !PlatformMemory::Commit(static_cast<uint8*>(range.Ptr) + page, stackSize))
		{
			PlatformMemory::Release(range.Ptr, range.Size);
			return false;
		}
		fiber.Stack = range;
		fiber.StackSize = stackSize;
		fiber.OwnsStack = true;
		void* top = static_cast<uint8*>(range.Ptr) + range.Size;
		fiber.Context = PrepareStack(top, entry, arg);
		return true;
#endif
	}

	void Destroy(PlatformFiber& fiber)
	{
		if (!fiber.OwnsStack)
			return;
#if FP_PLATFORM_WINDOWS
		if (fiber.Context)
			DeleteFiber(fiber.Context);
#else
		PlatformMemory::Release(fiber.Stack.Ptr, fiber.Stack.Size);
#endif
		fiber = PlatformFiber();
	}

	void ConvertThread(PlatformFiber& threadFiber)
	{
		threadFiber = PlatformFiber();
#if FP_PLATFORM_WINDOWS
		threadFiber.Context = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
#endif
		// Elsewhere the thread's own stack needs no setup. Its registers are saved on the first switch away.
	}

	void RevertThread(PlatformFiber& threadFiber)
	{
#if FP_PLATFORM_WINDOWS
		if (threadFiber.Context)
			ConvertFiberToThread();
#endif
		threadFiber = PlatformFiber();
	}

	void Switch(PlatformFiber& from, PlatformFiber& to)
	{
#if FP_PLATFORM_WINDOWS
		(void)from;
		SwitchToFiber(to.Context);
#else
		fp_fiber_switch(&from.Context, to.Context);
#endif
	}
}
