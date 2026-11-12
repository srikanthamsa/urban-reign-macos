#pragma once

// Single entry point for the SSE/SSE2/SSE4.1 intrinsics the port uses. On Apple Silicon sse2neon maps them to NEON;
// everywhere else they are the compiler's native x86 headers. Keep direct <immintrin.h>/<smmintrin.h> includes out of
// the runtime and host sources so the arm64 build cannot accidentally pull in x86 headers.
#if defined(USE_SSE2NEON)
#include "sse2neon.h"
#else
#include <xmmintrin.h>
#include <emmintrin.h>
#include <pmmintrin.h>
#include <smmintrin.h>
#include <immintrin.h>
#endif
