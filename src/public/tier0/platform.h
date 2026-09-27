//========= Copyright (c) 2026 WiseDev. MIT License. =========================//
//
// Purpose: Fixed-size integer typedefs.
//
//============================================================================//

#ifndef PLATFORM_H
#define PLATFORM_H
#pragma once

#include <cstddef>
#include <cstdint>

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

typedef uint64 JobID_t;
const JobID_t k_GIDNil = 0xffffffffffffffffull;

#define V_ARRAYSIZE( a ) ( sizeof( a ) / sizeof( ( a )[0] ) )

#endif // PLATFORM_H
