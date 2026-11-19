#pragma once
#ifndef CAMPUSLINK_VERSION
#error CAMPUSLINK_VERSION must be supplied from VERSION
#endif
#ifndef CAMPUSLINK_BUILD
#define CAMPUSLINK_BUILD local
#endif
#define CL_BUILD_STR_IMPL(x) #x
#define CL_BUILD_STR(x) CL_BUILD_STR_IMPL(x)
namespace campuslink::build {
inline constexpr char version[] = CL_BUILD_STR(CAMPUSLINK_VERSION);
inline constexpr char revision[] = CL_BUILD_STR(CAMPUSLINK_BUILD);
#if defined(_WIN32)
inline constexpr char system[] = "Windows 11";
#elif defined(__linux__)
inline constexpr char system[] = "Linux";
#else
inline constexpr char system[] = "Unknown";
#endif
#if defined(_M_X64) || defined(__x86_64__)
inline constexpr char architecture[] = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
inline constexpr char architecture[] = "ARM64";
#else
inline constexpr char architecture[] = "Unknown";
#endif
#if defined(__clang__)
inline constexpr char compiler[] = "Clang " __clang_version__;
#elif defined(__GNUC__)
inline constexpr char compiler[] = "GCC " __VERSION__;
#elif defined(_MSC_VER)
inline constexpr char compiler[] = "MSVC " CL_BUILD_STR(_MSC_VER);
#else
inline constexpr char compiler[] = "Unknown";
#endif
#ifdef NDEBUG
inline constexpr char configuration[] = "Release";
#else
inline constexpr char configuration[] = "Debug";
#endif
}
