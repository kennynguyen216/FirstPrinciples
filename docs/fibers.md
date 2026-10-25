# Fibers and context switching

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

A fiber is a resumable execution context with its own stack. It lets a job pause inside an ordinary
C++ call chain while the worker thread runs other work. This supports nested job waits without
creating a new OS thread for every dependency.

Game code normally uses [JobSystem](jobs.md). The platform fiber API is for the scheduler and other
code that needs explicit control over stacks and context switches.

## Platform API

| Operation | What it does |
|---|---|
| Create | Prepares a stack and initial context for an entry function and argument |
| ConvertThread | Wraps the calling thread's execution context so it can switch to fibers |
| Switch | Saves the current context and resumes another context |
| Destroy | Releases the resources of a fiber that is no longer running |
| RevertThread | Reverses thread conversion after fiber use ends |

A fiber entry function must never return. It should switch back to a scheduler or continue its own
dispatch loop. A fiber must not execute on two threads at once, and its stack must remain alive
while a suspended call chain still uses it.

## How it works

PlatformFiber stores the saved context, stack information and ownership state. On macOS and Linux,
PlatformFiber.cpp implements context switching for supported architectures by saving the required
register state and exchanging stack pointers. Windows uses the Win32 fiber API. These platform
implementations support the engine's macOS and Windows builds, but do not establish full Linux
support for the rest of the engine and editor.

Fiber stack sizes are rounded to whole pages. The custom-stack path leaves a guard page below the
usable stack so overflow faults rather than writing silently into another allocation. The scheduler
switches between its thread context and a job fiber when work finishes or waits.

FiberPool creates a fixed set of fibers before work starts. Acquire and Release use a mutex,
returning a null pointer when every fiber is in use. Releasing a fiber clears its UserData and makes
its existing stack available for another job. The pool must be shut down only after its fibers have
stopped executing or waiting.

A suspended job can resume on another worker thread. Do not assume that thread-local state, thread
identity or thread-affine resources remain valid across a job wait.

Source: [PlatformFiber.h](../src/HAL/PlatformFiber.h),
[PlatformFiber.cpp](../src/HAL/PlatformFiber.cpp), [FiberPool.h](../src/Core/Jobs/FiberPool.h),
[JobSystem.cpp](../src/Core/Jobs/JobSystem.cpp).

## Checks and related guides

Run `Projects/Sample/Binaries/Debug/Tests Fiber` for context, register, stack and pool checks. See
[job scheduling](jobs.md) for the gameplay-facing API and [memory](memory.md) for page storage.
