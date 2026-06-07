// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2023, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 * Ported for kernel by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 *
 * Updated to LZ4 v1.10.0 by kernel backport.
 * Key changes vs old kernel port:
 *  - clearedTable + tableType in LZ4_stream_t_internal (Fast Reset)
 *  - LZ4_prepareTable() avoids full memset on re-use
 *  - usingDictCtx dictionary mode
 *  - fillOutput limitedOutput_directive
 *  - LZ4_resetStream_fast() exported
 */

/*-************************************
 *	Dependencies
 **************************************/
#include "lz4defs.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>

/*-************************************
 *	Local definitions (not in header)
 **************************************/
static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT - 1));

/*-************************************
 *	Table management / Fast Reset
 **************************************/

/**
 * LZ4_prepareTable() - Prepare hash table for compression.
 *
 * If the table was previously used with a compatible type and is still
 * within usable range, it is reused as-is (Fast Reset path).
 * Otherwise it is zeroed (Slow Reset path).
 *
 * This is the key optimization added in LZ4 v1.9.0 / v1.10.0:
 * avoids expensive memset for repeated small compression calls (e.g. zram).
 */
static void LZ4_prepareTable(
	LZ4_stream_t_internal *const cctx,
	const int inputSize,
	const tableType_t tableType)
{
	if ((tableType_t)cctx->tableType != clearedTable) {
		if ((tableType_t)cctx->tableType != tableType
		  || ((tableType == byU16) &&
		      cctx->currentOffset + (unsigned)inputSize >= 0xFFFFU)
		  || ((tableType == byU32) && cctx->currentOffset > 1 * GB)
		  || tableType == byPtr
		  || inputSize >= 4 * KB) {
			/* Slow Reset — needs full clear */
			memset(cctx->hashTable, 0, LZ4_HASHTABLESIZE);
			cctx->currentOffset = 0;
			cctx->tableType = (U32)clearedTable;
		}
		/* else: Fast Reset — table is still valid, reuse as-is */
	}

	/* Add a gap so all previous entries become > LZ4_DISTANCE_MAX back */
	if (cctx->currentOffset != 0 && tableType == byU32)
		cctx->currentOffset += 64 * KB;

	/* Clear history (but not the hash table itself in fast path) */
	cctx->dictCtx = NULL;
	cctx->dictionary = NULL;
	cctx->dictSize = 0;
}

/*-************************************
 *	Hash functions
 **************************************/
static FORCE_INLINE U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
	if (tableType == byU16)
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
	else
		return ((sequence * 2654435761U)
			>> ((MINMATCH * 8) - LZ4_HASHLOG));
}

static FORCE_INLINE U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
	const U32 hashLog = (tableType == byU16)
		? LZ4_HASHLOG + 1
		: LZ4_HASHLOG;

#if LZ4_LITTLE_ENDIAN
	static const U64 prime5bytes = 889523592379ULL;

	return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
#else
	static const U64 prime8bytes = 11400714785074694791ULL;

	return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
#endif
}

static FORCE_INLINE U32 LZ4_hashPosition(
	const void *p,
	tableType_t const tableType)
{
#if LZ4_ARCH64
	if (tableType != byU16)
		return LZ4_hash5(LZ4_read_ARCH(p), tableType);
#endif

	return LZ4_hash4(LZ4_read32(p), tableType);
}

/*-************************************
 *	Table put/get helpers
 **************************************/

/* byPtr only */
static FORCE_INLINE void LZ4_putPositionOnHash(
	const BYTE *p,
	U32 h,
	void *tableBase,
	tableType_t const tableType,
	const BYTE *srcBase)
{
	switch (tableType) {
	case byPtr: {
		const BYTE **hashTable = (const BYTE **)tableBase;
		hashTable[h] = p;
		return;
	}
	case byU32: {
		U32 *hashTable = (U32 *)tableBase;
		hashTable[h] = (U32)(p - srcBase);
		return;
	}
	case byU16: {
		U16 *hashTable = (U16 *)tableBase;
		hashTable[h] = (U16)(p - srcBase);
		return;
	}
	default:
		return;
	}
}

static FORCE_INLINE void LZ4_putPosition(
	const BYTE *p,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
}

/* Index-based put (byU32 / byU16) */
static FORCE_INLINE void LZ4_putIndexOnHash(
	U32 idx,
	U32 h,
	void *tableBase,
	tableType_t const tableType)
{
	switch (tableType) {
	case byU32: {
		U32 *hashTable = (U32 *)tableBase;
		hashTable[h] = idx;
		return;
	}
	case byU16: {
		U16 *hashTable = (U16 *)tableBase;
		hashTable[h] = (U16)idx;
		return;
	}
	default:
		return;
	}
}

static FORCE_INLINE U32 LZ4_getIndexOnHash(
	U32 h,
	const void *tableBase,
	tableType_t tableType)
{
	if (tableType == byU32) {
		const U32 *const hashTable = (const U32 *)tableBase;
		return hashTable[h];
	}
	if (tableType == byU16) {
		const U16 *const hashTable = (const U16 *)tableBase;
		return hashTable[h];
	}
	return 0;
}

static const BYTE *LZ4_getPositionOnHash(
	U32 h,
	const void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	if (tableType == byPtr) {
		const BYTE **hashTable = (const BYTE **)tableBase;

		return hashTable[h];
	}

	if (tableType == byU32) {
		const U32 *const hashTable = (U32 *)tableBase;

		return hashTable[h] + srcBase;
	}

	{
		const U16 *const hashTable = (U16 *)tableBase;

		return hashTable[h] + srcBase;
	}
}

static FORCE_INLINE const BYTE *LZ4_getPosition(
	const BYTE *p,
	const void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	return LZ4_getPositionOnHash(h, tableBase, tableType, srcBase);
}

/*-************************************
 *	Core compression
 **************************************/

/**
 * LZ4_compress_generic_validated() -
 * Main compression loop. Inlined to enable compile-time branch elimination.
 *
 * Caller must ensure: source != NULL, inputSize > 0.
 */
static FORCE_INLINE int LZ4_compress_generic_validated(
	LZ4_stream_t_internal *const cctx,
	const char *const source,
	char *const dest,
	const int inputSize,
	int *inputConsumed, /* only written when outputDirective == fillOutput */
	const int maxOutputSize,
	const limitedOutput_directive outputDirective,
	const tableType_t tableType,
	const dict_directive dictDirective,
	const dictIssue_directive dictIssue,
	const int acceleration)
{
	int result;
	const BYTE *ip = (const BYTE *)source;

	U32 const startIndex = cctx->currentOffset;
	const BYTE *base = (const BYTE *)source - startIndex;
	const BYTE *lowLimit;

	const LZ4_stream_t_internal *dictCtx = cctx->dictCtx;
	const BYTE *const dictionary =
		(dictDirective == usingDictCtx) ? dictCtx->dictionary : cctx->dictionary;
	const U32 dictSize =
		(dictDirective == usingDictCtx) ? dictCtx->dictSize : cctx->dictSize;
	const U32 dictDelta =
		(dictDirective == usingDictCtx) ? startIndex - dictCtx->currentOffset : 0;

	const BYTE *anchor = (const BYTE *)source;
	const BYTE *const iend = ip + inputSize;
	const BYTE *const mflimitPlusOne = iend - MFLIMIT + 1;
	const BYTE *const matchlimit = iend - LASTLITERALS;

	BYTE *op = (BYTE *)dest;
	BYTE *const olimit = op + maxOutputSize;

	U32 offset = 0;
	U32 forwardH;
	size_t refDelta = 0;

	/* If compression fails and outputDirective == fillOutput, ip is tracked */
	if (outputDirective == fillOutput && inputConsumed)
		*inputConsumed = 0;

	/* Set lowLimit */
	if (dictDirective == noDict) {
		lowLimit = (const BYTE *)source;
	} else if (dictDirective == usingDictCtx) {
		lowLimit = (const BYTE *)source;
	} else {
		lowLimit = (const BYTE *)source - cctx->dictSize;
	}

	/* First Byte */
	LZ4_putPosition(ip, cctx->hashTable, tableType, base);
	ip++;
	forwardH = LZ4_hashPosition(ip, tableType);

	/* Main Loop */
	for (;;) {
		const BYTE *match;
		BYTE *token;
		const BYTE *filledIp;

		/* Find a match */
		{
			const BYTE *forwardIp = ip;
			unsigned int step = 1;
			unsigned int searchMatchNb = acceleration << LZ4_SKIPTRIGGER;

			do {
				U32 const h = forwardH;
				U32 const curr = (U32)(forwardIp - base);
				U32 matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);

				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_SKIPTRIGGER);

				if (unlikely(forwardIp > mflimitPlusOne))
					goto _last_literals;

				if (tableType == byPtr) {
					match = LZ4_getPositionOnHash(h, cctx->hashTable, tableType, base);
					LZ4_putPositionOnHash(ip, h, cctx->hashTable, tableType, base);
					forwardH = LZ4_hashPosition(forwardIp, tableType);

					if ((match + LZ4_DISTANCE_MAX >= ip) &&
					    (LZ4_read32(match) == LZ4_read32(ip)))
						break;
					continue;
				}

				/* byU16 / byU32 path */
				LZ4_putIndexOnHash(curr, h, cctx->hashTable, tableType);
				forwardH = LZ4_hashPosition(forwardIp, tableType);

				if (dictDirective == usingDictCtx) {
					if (matchIndex < startIndex) {
						/* reference into dictionary */
						U32 const dictMatchIndex = dictCtx->hashTable[h];
						if (dictMatchIndex >= dictCtx->dictSize)
							continue;
						match = dictionary + dictMatchIndex;
						refDelta = dictDelta;
						lowLimit = dictionary;
					} else {
						match = base + matchIndex;
						refDelta = 0;
						lowLimit = (const BYTE *)source;
					}
				} else if (dictDirective == usingExtDict) {
					if (matchIndex < startIndex) {
						match = cctx->dictionary + (matchIndex - (startIndex - dictSize));
						refDelta = (ptrdiff_t)(cctx->dictionary) - (ptrdiff_t)(source - startIndex);
						lowLimit = cctx->dictionary;
					} else {
						match = base + matchIndex;
						refDelta = 0;
						lowLimit = (const BYTE *)source;
					}
				} else {
					match = base + matchIndex;
					refDelta = 0;
				}

				offset = curr - matchIndex;
				if (dictIssue == dictSmall && (matchIndex < (U32)dictSize))
					continue;

				if ((tableType != byU16) && (offset >= LZ4_DISTANCE_MAX))
					continue;

				if (LZ4_read32(match + refDelta) != LZ4_read32(ip))
					continue;

				break;
			} while (1);

			filledIp = forwardIp;
			(void)filledIp;
		}

		/* Catch up: find earliest match position */
		while (((ip > anchor) & (match + refDelta > lowLimit))
		       && (unlikely(ip[-1] == (match + refDelta)[-1]))) {
			ip--;
			match--;
		}

		/* Encode Literals */
		{
			unsigned const litLength = (unsigned)(ip - anchor);

			token = op++;

			if ((outputDirective == limitedOutput) &&
			    (unlikely(op + litLength +
				      (2 + 1 + LASTLITERALS) +
				      (litLength / 255) > olimit))) {
				return 0;
			}
			if ((outputDirective == fillOutput) &&
			    (unlikely(op + (litLength + 240) / 255 + litLength + 2 > olimit))) {
				op--;
				goto _last_literals;
			}

			if (litLength >= RUN_MASK) {
				int len = (int)litLength - RUN_MASK;

				*token = (RUN_MASK << ML_BITS);

				for (; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = (BYTE)len;
			} else {
				*token = (BYTE)(litLength << ML_BITS);
			}

			/* Copy Literals */
			LZ4_wildCopy8(op, anchor, op + litLength);
			op += litLength;
		}

_next_match:
		/* Encode Offset */
		if (tableType == byPtr) {
			LZ4_writeLE16(op, (U16)(ip - match));
		} else {
			LZ4_writeLE16(op, (U16)offset);
		}
		op += 2;

		/* Encode MatchLength */
		{
			unsigned int matchCode;
			const BYTE *matchPtr = match + refDelta;

			if (dictDirective == usingExtDict && lowLimit == cctx->dictionary) {
				const BYTE *limit;
				const BYTE *dictEnd = cctx->dictionary + cctx->dictSize;

				limit = ip + (dictEnd - matchPtr);
				if (limit > matchlimit)
					limit = matchlimit;

				matchCode = LZ4_count(ip + MINMATCH, matchPtr + MINMATCH, limit);
				ip += MINMATCH + matchCode;

				if (ip == limit) {
					unsigned const more = LZ4_count(ip,
						(const BYTE *)source, matchlimit);

					matchCode += more;
					ip += more;
				}
			} else {
				matchCode = LZ4_count(ip + MINMATCH,
					matchPtr + MINMATCH, matchlimit);
				ip += MINMATCH + matchCode;
			}

			if ((outputDirective == limitedOutput) &&
			    (unlikely(op + (1 + LASTLITERALS) +
				      (matchCode >> 8) > olimit)))
				return 0;

			if ((outputDirective == fillOutput) &&
			    (unlikely(op + (matchCode >> 8) + 1 > olimit))) {
				/* matchCode is too long to fit, truncate */
				unsigned const newMatchCode = 15 + (int)(olimit - op) * 255;

				ip -= matchCode - newMatchCode;
				matchCode = newMatchCode;
			}

			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);

				while (matchCode >= 4 * 255) {
					op += 4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4 * 255;
				}

				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			} else {
				*token += (BYTE)(matchCode);
			}
		}

		anchor = ip;

		/* Test end of chunk */
		if (ip >= mflimitPlusOne)
			break;

		/* Fill table */
		LZ4_putPosition(ip - 2, cctx->hashTable, tableType, base);

		/* Test next position */
		if (tableType == byPtr) {
			match = LZ4_getPosition(ip, cctx->hashTable, tableType, base);
			LZ4_putPosition(ip, cctx->hashTable, tableType, base);

			offset = (U32)(ip - match);
			if (offset <= LZ4_DISTANCE_MAX &&
			    (LZ4_read32(match) == LZ4_read32(ip))) {
				token = op++;
				*token = 0;
				refDelta = 0;
				goto _next_match;
			}
		} else {
			U32 const h = LZ4_hashPosition(ip, tableType);
			U32 const curr = (U32)(ip - base);
			U32 const matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);

			LZ4_putIndexOnHash(curr, h, cctx->hashTable, tableType);

			if (matchIndex + LZ4_DISTANCE_MAX >= curr) {
				const BYTE *matchPtr;

				if (tableType == byU32)
					matchPtr = base + matchIndex;
				else
					matchPtr = (const BYTE *)source + matchIndex;

				if (LZ4_read32(matchPtr) == LZ4_read32(ip)) {
					token = op++;
					*token = 0;
					offset = curr - matchIndex;
					refDelta = 0;
					goto _next_match;
				}
			}
		}

		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		size_t const lastRun = (size_t)(iend - anchor);

		if ((outputDirective != notLimited) &&
		    ((op - (BYTE *)dest) + lastRun + 1 +
		     ((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize)) {
			if (outputDirective == fillOutput) {
				/* limited space: encode only what fits */
				size_t const newLastRun =
					(olimit - op) - 1 - (((olimit - op) - 1 + 240) / 255);
				if (inputConsumed)
					*inputConsumed = (int)(anchor - (const BYTE *)source) + (int)newLastRun;

				if (newLastRun >= RUN_MASK) {
					size_t accumulator = newLastRun - RUN_MASK;
					*op++ = RUN_MASK << ML_BITS;
					for (; accumulator >= 255; accumulator -= 255)
						*op++ = 255;
					*op++ = (BYTE)accumulator;
				} else {
					*op++ = (BYTE)(newLastRun << ML_BITS);
				}
				LZ4_memcpy(op, anchor, newLastRun);
				op += newLastRun;
				result = (int)(((char *)op) - dest);
				return result;
			}
			return 0; /* limitedOutput */
		}

		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;
			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255)
				*op++ = 255;
			*op++ = (BYTE)accumulator;
		} else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}

		LZ4_memcpy(op, anchor, lastRun);
		op += lastRun;
	}

	if (outputDirective == fillOutput && inputConsumed)
		*inputConsumed = inputSize;

	result = (int)(((char *)op) - dest);
	return result;
}

/**
 * LZ4_compress_generic() - Wrapper adding NULL/inputSize checks.
 */
static FORCE_INLINE int LZ4_compress_generic(
	LZ4_stream_t_internal *const cctx,
	const char *const source,
	char *const dest,
	const int inputSize,
	int *inputConsumed,
	const int maxOutputSize,
	const limitedOutput_directive outputDirective,
	const tableType_t tableType,
	const dict_directive dictDirective,
	const dictIssue_directive dictIssue,
	const int acceleration)
{
	if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE)
		return 0;
	if (inputSize == 0) {
		if (outputDirective != notLimited && maxOutputSize < 1)
			return 0;
		if (source == NULL)
			return 0;
		/* empty input: just write one zero byte (no-match sequence) */
		*dest = 0;
		if (outputDirective == fillOutput && inputConsumed)
			*inputConsumed = 0;
		return 1;
	}
	return LZ4_compress_generic_validated(cctx, source, dest,
		inputSize, inputConsumed, maxOutputSize,
		outputDirective, tableType, dictDirective,
		dictIssue, acceleration);
}

/*-************************************
 *	Public compression functions
 **************************************/

static int LZ4_compress_fast_extState(
	void *state,
	const char *source,
	char *dest,
	int inputSize,
	int maxOutputSize,
	int acceleration)
{
	LZ4_stream_t_internal *ctx = &((LZ4_stream_t *)state)->internal_donotuse;
#if LZ4_ARCH64
	const tableType_t tableType = byU32;
#else
	const tableType_t tableType = byPtr;
#endif

	LZ4_prepareTable(ctx, inputSize, tableType);

	if (maxOutputSize >= LZ4_COMPRESSBOUND(inputSize)) {
		if (inputSize < LZ4_64Klimit)
			return LZ4_compress_generic(ctx, source, dest,
				inputSize, NULL, 0,
				notLimited, byU16, noDict,
				noDictIssue, acceleration);
		else
			return LZ4_compress_generic(ctx, source, dest,
				inputSize, NULL, 0,
				notLimited, tableType, noDict,
				noDictIssue, acceleration);
	} else {
		if (inputSize < LZ4_64Klimit)
			return LZ4_compress_generic(ctx, source, dest,
				inputSize, NULL, maxOutputSize,
				limitedOutput, byU16, noDict,
				noDictIssue, acceleration);
		else
			return LZ4_compress_generic(ctx, source, dest,
				inputSize, NULL, maxOutputSize,
				limitedOutput, tableType, noDict,
				noDictIssue, acceleration);
	}
}

int LZ4_compress_fast(const char *source, char *dest, int inputSize,
	int maxOutputSize, int acceleration, void *wrkmem)
{
	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;
	if (acceleration > LZ4_ACCELERATION_MAX)
		acceleration = LZ4_ACCELERATION_MAX;

	return LZ4_compress_fast_extState(wrkmem, source, dest,
		inputSize, maxOutputSize, acceleration);
}
EXPORT_SYMBOL(LZ4_compress_fast);

int LZ4_compress_default(const char *source, char *dest, int inputSize,
	int maxOutputSize, void *wrkmem)
{
	return LZ4_compress_fast(source, dest, inputSize,
		maxOutputSize, LZ4_ACCELERATION_DEFAULT, wrkmem);
}
EXPORT_SYMBOL(LZ4_compress_default);

int LZ4_compress_destSize(
	const char *source,
	char *dest,
	int *sourceSizePtr,
	int targetDestSize,
	void *wrkmem)
{
	LZ4_stream_t_internal *ctx = &((LZ4_stream_t *)wrkmem)->internal_donotuse;
#if LZ4_ARCH64
	const tableType_t tableType = byU32;
#else
	const tableType_t tableType = byPtr;
#endif
	LZ4_prepareTable(ctx, *sourceSizePtr, tableType);

	return LZ4_compress_generic(ctx, source, dest,
		*sourceSizePtr, sourceSizePtr, targetDestSize,
		fillOutput, tableType, noDict, noDictIssue,
		LZ4_ACCELERATION_DEFAULT);
}
EXPORT_SYMBOL(LZ4_compress_destSize);

/*-************************************
 *	Streaming functions
 **************************************/
void LZ4_resetStream(LZ4_stream_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}
EXPORT_SYMBOL(LZ4_resetStream);

/**
 * LZ4_resetStream_fast() - Fast reset (v1.9.0+)
 *
 * Only safe if the context was previously initialized via LZ4_resetStream()
 * or LZ4_compress_fast_extState(). Skips memset; relies on tableType tracking.
 */
void LZ4_resetStream_fast(LZ4_stream_t *ctx)
{
	LZ4_prepareTable(&ctx->internal_donotuse, 0, byU32);
}
EXPORT_SYMBOL(LZ4_resetStream_fast);

int LZ4_loadDict(LZ4_stream_t *LZ4_dict,
	const char *dictionary, int dictSize)
{
	LZ4_stream_t_internal *dict = &LZ4_dict->internal_donotuse;
	const tableType_t tableType = byU32;
	const BYTE *p = (const BYTE *)dictionary;
	const BYTE *const dictEnd = p + dictSize;
	const BYTE *base;

	/* Reset and setup offsets */
	LZ4_prepareTable(dict, 0, tableType);

	if (dict->currentOffset != 0 && tableType == byU32)
		dict->currentOffset -= 64 * KB; /* undo the 64KB gap added by prepareTable */

	if (dictSize < (int)HASH_UNIT) {
		dict->dictionary = NULL;
		dict->dictSize = 0;
		return 0;
	}

	if ((dictEnd - p) > 64 * KB)
		p = dictEnd - 64 * KB;

	base = p - dict->currentOffset;
	dict->dictionary = p;
	dict->dictSize = (U32)(dictEnd - p);
	dict->currentOffset += dict->dictSize;

	while (p <= dictEnd - HASH_UNIT) {
		LZ4_putPosition(p, dict->hashTable, byU32, base);
		p += 3;
	}

	return (int)dict->dictSize;
}
EXPORT_SYMBOL(LZ4_loadDict);

int LZ4_saveDict(LZ4_stream_t *LZ4_dict, char *safeBuffer, int dictSize)
{
	LZ4_stream_t_internal *const dict = &LZ4_dict->internal_donotuse;
	const BYTE *const previousDictEnd = dict->dictionary + dict->dictSize;

	if ((U32)dictSize > 64 * KB)
		dictSize = 64 * KB; /* never return more than 64 KB */

	if ((U32)dictSize > dict->dictSize)
		dictSize = (int)dict->dictSize;

	memmove(safeBuffer, previousDictEnd - dictSize, dictSize);

	dict->dictionary = (const BYTE *)safeBuffer;
	dict->dictSize = (U32)dictSize;

	return dictSize;
}
EXPORT_SYMBOL(LZ4_saveDict);

static void LZ4_renormDictT(LZ4_stream_t_internal *LZ4_dict, int nextSize)
{
	if (LZ4_dict->currentOffset + (unsigned)nextSize > 0x80000000) {   /* potential ptrdiff_t overflow (32-bits mode) */
		/* rescale hash table */
		U32 const delta = LZ4_dict->currentOffset - 64 * KB;
		const BYTE *dictEnd = LZ4_dict->dictionary + LZ4_dict->dictSize;
		int i;

		for (i = 0; i < LZ4_HASH_SIZE_U32; i++) {
			if (LZ4_dict->hashTable[i] < delta)
				LZ4_dict->hashTable[i] = 0;
			else
				LZ4_dict->hashTable[i] -= delta;
		}
		LZ4_dict->currentOffset = 64 * KB;
		if (LZ4_dict->dictSize > 64 * KB)
			LZ4_dict->dictSize = 64 * KB;
		LZ4_dict->dictionary = dictEnd - LZ4_dict->dictSize;
	}
}

int LZ4_compress_fast_continue(
	LZ4_stream_t *LZ4_stream,
	const char *source,
	char *dest,
	int inputSize,
	int maxOutputSize,
	int acceleration)
{
	const tableType_t tableType = byU32;
	LZ4_stream_t_internal *const streamPtr = &LZ4_stream->internal_donotuse;
	const BYTE *dictEnd = streamPtr->dictionary + streamPtr->dictSize;

	LZ4_renormDictT(streamPtr, inputSize);   /* fix index overflow */

	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;
	if (acceleration > LZ4_ACCELERATION_MAX)
		acceleration = LZ4_ACCELERATION_MAX;

	/* check and update state */
	if (streamPtr->tableType == (U32)clearedTable) {
		/* first call, nothing to build on */
		return LZ4_compress_fast_extState(LZ4_stream, source, dest,
			inputSize, maxOutputSize, acceleration);
	}

	if ((U32)inputSize > 1 * GB)
		return 0;
	if (streamPtr->currentOffset > 1 * GB) {
		/* overflow: must reset */
		LZ4_resetStream(LZ4_stream);
		return LZ4_compress_fast_extState(LZ4_stream, source, dest,
			inputSize, maxOutputSize, acceleration);
	}

	/* Handle block links */
	if (dictEnd == (const BYTE *)source) {
		/* contiguous */
		if (streamPtr->dictSize >= 64 * KB) {
			return LZ4_compress_generic(streamPtr, source, dest,
				inputSize, NULL, maxOutputSize,
				maxOutputSize >= LZ4_COMPRESSBOUND(inputSize) ?
					notLimited : limitedOutput,
				tableType, withPrefix64k, noDictIssue, acceleration);
		}
		return LZ4_compress_generic(streamPtr, source, dest,
			inputSize, NULL, maxOutputSize,
			maxOutputSize >= LZ4_COMPRESSBOUND(inputSize) ?
				notLimited : limitedOutput,
			tableType, noDict, noDictIssue, acceleration);
	} else {
		/* non-contiguous: use usingExtDict */
		return LZ4_compress_generic(streamPtr, source, dest,
			inputSize, NULL, maxOutputSize,
			maxOutputSize >= LZ4_COMPRESSBOUND(inputSize) ?
				notLimited : limitedOutput,
			tableType, usingExtDict, noDictIssue, acceleration);
	}
}
EXPORT_SYMBOL(LZ4_compress_fast_continue);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 compressor — v1.10.0 kernel port");
