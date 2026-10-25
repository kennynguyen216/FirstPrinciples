# Job scheduling

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

Jobs divide independent work across worker threads. Fiber-based waits allow nested work to pause
without leaving a worker blocked on a dependency.

## Usage and behavior

`FP::JobSystem` spreads work over worker threads, defaulting to one fewer than the hardware thread
count with at least one worker. Jobs run on fibers, small stacks the engine switches between in user
space, so a job that waits on other jobs parks its fiber and the thread moves on instead of
blocking. A fiber switch costs tens of nanoseconds where starting an OS thread costs tens of
microseconds.

```cpp
#include "Core/Jobs/JobSystem.h"

FP::JobCounter counter;
for (int32 i = 0; i < chunkCount; ++i)
    FP::JobSystem::Run([i]() { ProcessChunk(i); }, &counter);
FP::JobSystem::Wait(counter);

FP::JobSystem::ParallelFor(enemyCount, 32, [&](int32 i) { UpdateEnemy(i); });
```

- Jobs can run and wait on more jobs. Waiting inside a job never ties up a thread.
- The editor and runtime start the system for you. The main thread helps run jobs while it waits.
- Job lambdas hold up to 48 bytes of captures. Capture large data by reference.
- A job can resume on a different thread after a wait, so avoid `thread_local` inside jobs.
- Never spin inside a job waiting for the main thread. It may be the thread running that job.
- Systems update on the main thread. Use jobs for work that does not touch the world from many
  jobs at once, such as math over arrays or loading.

## Configuration and lifetime

Standalone tools can pass a JobSystemDesc to Initialize to set WorkerCount, FiberCount and
FiberStackSize. Defaults are 128 fibers with 128 KiB of usable stack each. Keep counters and
captured references alive until their jobs finish. Finish all jobs before Shutdown.

## How it works

JobSystem queues jobs by priority and assigns work to pooled fibers. Waiting jobs hand their fiber
back to the scheduler, which resumes eligible fibers when counters reach their targets. The
initializing thread also helps execute work during waits. Queue operations use a mutex, and pooled
fibers bound how much work can be active or waiting at once.

Source: [JobSystem.h](../src/Core/Jobs/JobSystem.h),
[JobSystem.cpp](../src/Core/Jobs/JobSystem.cpp), [FiberPool.h](../src/Core/Jobs/FiberPool.h).

## Related guides

[Fiber implementation](fibers.md), [Memory allocation](memory.md), [Benchmarks](benchmarks.md).
