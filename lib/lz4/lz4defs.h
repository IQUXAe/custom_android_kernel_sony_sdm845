/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
#ifndef __LZ4DEFS_H__
#define __LZ4DEFS_H__

/*
 * lz4defs.h -- common and architecture specific defines for the kernel usage
 *
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2023, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Ported to kernel by:
 *  Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 * Updated to LZ4 v1.10.0 API by:
 *  Kernel backport
 */

#include <asm/unaligned.h>
#include <linux/bitops.h>
#include <linux/string.h>	/* memset, memcpy */
#include <linux/lz4.h>

#define FORCE_INLINE __always_inline

/*-************************************
 *	Basic Types
 **************************************/
#include <linux/types.h>

typedef	uint8_t  BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef	int32_t  S32;
typedef uint64_t U64;
typedef uintptr_t uptrval;

#if defined(CONFIG_64BIT)
typedef U64 reg_t;
#else
typedef U32 reg_t;
#endif

/*-************************************
 *	Architecture specifics
 **************************************/
#if defined(CONFIG_64BIT)
# define LZ4_ARCH64 1
#else
# define LZ4_ARCH64 0
#endif

#if defined(__LITTLE_ENDIAN)
# define LZ4_LITTLE_ENDIAN 1
#else
# define LZ4_LITTLE_ENDIAN 0
#endif

/*-************************************
 *	Constants
 **************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS   5
#define MFLIMIT        (WILDCOPYLENGTH + MINMATCH)

/*
 * ensure it's possible to write 2 x wildcopyLength
 * without overflowing output buffer
 */
#define MATCH_SAFEGUARD_DISTANCE  ((2 * WILDCOPYLENGTH) - MINMATCH)
#define FASTLOOP_SAFE_DISTANCE    64

/* Increase this value ==> compression run slower on incompressible data */
#define LZ4_SKIPTRIGGER 6

#define HASH_UNIT sizeof(reg_t)

#define KB (1 << 10)
#define MB (1 << 20)
#define GB (1U << 30)

#define LZ4_DISTANCE_MAX LZ4_DISTANCE_ABSOLUTE_MAX
#define STEPSIZE sizeof(reg_t)

#define ML_BITS  4
#define ML_MASK  ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

/*-************************************
 *	Reading and writing into memory
 **************************************/

/*
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so apply its specialized memcpy() inlining logic. When
 * possible, use __builtin_memcpy() to tell the compiler to analyze memcpy()
 * as-if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)
#define LZ4_memmove(dst, src, size) __builtin_memmove(dst, src, size)
#define LZ4_memset(p, v, s) __builtin_memset(p, v, s)

static FORCE_INLINE U16 LZ4_read16(const void *ptr)
{
	return get_unaligned((const U16 *)ptr);
}

static FORCE_INLINE U32 LZ4_read32(const void *ptr)
{
	return get_unaligned((const U32 *)ptr);
}

static FORCE_INLINE reg_t LZ4_read_ARCH(const void *ptr)
{
	return get_unaligned((const reg_t *)ptr);
}

static FORCE_INLINE void LZ4_write16(void *memPtr, U16 value)
{
	put_unaligned(value, (U16 *)memPtr);
}

static FORCE_INLINE void LZ4_write32(void *memPtr, U32 value)
{
	put_unaligned(value, (U32 *)memPtr);
}

static FORCE_INLINE U16 LZ4_readLE16(const void *memPtr)
{
	return get_unaligned_le16(memPtr);
}

static FORCE_INLINE void LZ4_writeLE16(void *memPtr, U16 value)
{
	return put_unaligned_le16(value, memPtr);
}

static FORCE_INLINE void LZ4_copy8(void *dst, const void *src)
{
#if LZ4_ARCH64
	U64 a = get_unaligned((const U64 *)src);
	put_unaligned(a, (U64 *)dst);
#else
	U32 a = get_unaligned((const U32 *)src);
	U32 b = get_unaligned((const U32 *)src + 1);
	put_unaligned(a, (U32 *)dst);
	put_unaligned(b, (U32 *)dst + 1);
#endif
}

/*
 * customized variant of memcpy,
 * which can overwrite up to 7 bytes beyond dstEnd
 */
static FORCE_INLINE void LZ4_wildCopy8(void *dstPtr,
	const void *srcPtr, void *dstEnd)
{
	BYTE *d = (BYTE *)dstPtr;
	const BYTE *s = (const BYTE *)srcPtr;
	BYTE *const e = (BYTE *)dstEnd;

	do {
		LZ4_copy8(d, s);
		d += 8;
		s += 8;
	} while (d < e);
}

static FORCE_INLINE unsigned int LZ4_NbCommonBytes(register reg_t val)
{
#if LZ4_LITTLE_ENDIAN
# if LZ4_ARCH64
	return __builtin_ctzll((U64)val) >> 3;
# else
	return __builtin_ctz((U32)val) >> 3;
# endif
#else
# if LZ4_ARCH64
	return __builtin_clzll((U64)val) >> 3;
# else
	return __builtin_clz((U32)val) >> 3;
# endif
#endif
}

static FORCE_INLINE unsigned int LZ4_count(
	const BYTE *pIn,
	const BYTE *pMatch,
	const BYTE *pInLimit)
{
	const BYTE *const pStart = pIn;

	if (likely(pIn < pInLimit - (STEPSIZE - 1))) {
		reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
		if (!diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
		} else {
			return LZ4_NbCommonBytes(diff);
		}
	}

	while (likely(pIn < pInLimit - (STEPSIZE - 1))) {
		reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
		if (!diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
			continue;
		}
		pIn += LZ4_NbCommonBytes(diff);
		return (unsigned int)(pIn - pStart);
	}

#if LZ4_ARCH64
	if ((pIn < (pInLimit - 3))
		&& (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
		pIn += 4;
		pMatch += 4;
	}
#endif

	if ((pIn < (pInLimit - 1))
		&& (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2;
		pMatch += 2;
	}

	if ((pIn < pInLimit) && (*pMatch == *pIn))
		pIn++;

	return (unsigned int)(pIn - pStart);
}

/* ===== Enums ===== */

/* clearedTable is new in 1.9.0 (Fast Reset) */
typedef enum { clearedTable = 0, byPtr, byU32, byU16 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict, usingDictCtx } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { notLimited = 0, limitedOutput = 1, fillOutput = 2 } limitedOutput_directive;
typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;

#define LZ4_STATIC_ASSERT(c) BUILD_BUG_ON(!(c))

#endif
