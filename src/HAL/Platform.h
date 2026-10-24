#pragma once

#include <cstddef>
#include <cstdint>

// ---- Platform detection ----------------------------------------------------
#if defined(_WIN32)
	#define FP_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
	#define FP_PLATFORM_MACOS 1
#elif defined(__linux__)
	#define FP_PLATFORM_LINUX 1
#else
	#error "Unsupported platform"
#endif

#ifndef FP_PLATFORM_WINDOWS
	#define FP_PLATFORM_WINDOWS 0
#endif
#ifndef FP_PLATFORM_MACOS
	#define FP_PLATFORM_MACOS 0
#endif
#ifndef FP_PLATFORM_LINUX
	#define FP_PLATFORM_LINUX 0
#endif

#define FP_PLATFORM_POSIX (FP_PLATFORM_MACOS || FP_PLATFORM_LINUX)

// ---- Compiler helpers ------------------------------------------------------
#if defined(_MSC_VER)
	#define FP_FORCEINLINE __forceinline
	#define FP_NOINLINE __declspec(noinline)
	#define FP_DEBUGBREAK() __debugbreak()
#else
	#define FP_FORCEINLINE inline __attribute__((always_inline))
	#define FP_NOINLINE __attribute__((noinline))
	#if defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
		#define FP_DEBUGBREAK() __builtin_debugtrap()
	#else
		#define FP_DEBUGBREAK() __builtin_trap()
	#endif
#endif

#if defined(FP_DEBUG)
	#define FP_BUILD_DEBUG 1
#else
	#define FP_BUILD_DEBUG 0
#endif

// ---- Fixed width types -----------------------------------------------------
namespace FP
{
	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;
	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;
	using usize = std::size_t;
	using isize = std::ptrdiff_t;
	using uptr = std::uintptr_t;
}
