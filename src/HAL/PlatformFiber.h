#pragma once

#include "HAL/PlatformMemory.h"

namespace FP
{
	// The lowest level of fibers: a stack and a saved register context you can switch into.
	//
	// A fiber switch saves only the callee saved registers and swaps the stack pointer, so it
	// costs a few nanoseconds with no kernel involvement. OS threads cost a syscall and a kernel
	// scheduler decision per switch and reserve megabytes of stack each. macOS and Linux use the
	// hand written switch in PlatformFiber.cpp. Windows uses the Win32 fiber API.
	struct PlatformFiber
	{
		using EntryFn = void (*)(void* arg);

		void* Context = nullptr; // saved stack pointer, or the Win32 fiber handle
		MemoryBlock Stack;       // reserved range including the guard page
		usize StackSize = 0;     // usable bytes above the guard page
		bool OwnsStack = false;
	};

	namespace PlatformFibers
	{
		// Makes a fiber that runs entry(arg) the first time it is switched to.
		// The stack is rounded up to whole pages with one guard page below it, so an overflow
		// faults right away instead of corrupting other memory. entry must never return.
		bool Create(PlatformFiber& fiber, usize stackSize, PlatformFiber::EntryFn entry, void* arg);
		void Destroy(PlatformFiber& fiber);

		// Lets the calling thread switch into fibers. Its own stack becomes the returned fiber.
		void ConvertThread(PlatformFiber& threadFiber);
		void RevertThread(PlatformFiber& threadFiber);

		// Saves the current registers into from and continues running to.
		void Switch(PlatformFiber& from, PlatformFiber& to);
	}
}
