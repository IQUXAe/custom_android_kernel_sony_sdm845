/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* LZ4 Kernel Interface
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 * Copyright (C) 2016, Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 * Copyright (C) 2023, Yann Collet — LZ4 v1.10.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file is based on the LZ4 v1.10.0 library header:
 *   LZ4 - Fast LZ compression algorithm
 *   Copyright (C) 2011-2023, Yann Collet.
 *   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 */

#ifndef __LZ4_H__
#define __LZ4_H__

#include <linux/types.h>
#include <linux/string.h>	/* memset, memcpy */

/*-************************************************************************
 *	VERSION
 **************************************************************************/
#define LZ4_VERSION_MAJOR	1
#define LZ4_VERSION_MINOR	10
#define LZ4_VERSION_RELEASE	0

#define LZ4_VERSION_NUMBER (LZ4_VERSION_MAJOR * 10000 \
			  + LZ4_VERSION_MINOR * 100  \
			  + LZ4_VERSION_RELEASE)

/*-************************************************************************
 *	CONSTANTS
 **************************************************************************/
/*
 * LZ4_MEMORY_USAGE :
 * Memory usage formula : N->2^N Bytes
 * (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
 * Increasing memory usage improves compression ratio
 * Reduced memory usage can improve speed, due to cache effect
 * Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
 */
#define LZ4_MEMORY_USAGE 14

#define LZ4_MAX_INPUT_SIZE	0x7E000000 /* 2 113 929 216 bytes */
#define LZ4_COMPRESSBOUND(isize)	(\
	(unsigned int)(isize) > (unsigned int)LZ4_MAX_INPUT_SIZE \
	? 0 \
	: (isize) + ((isize)/255) + 16)

/* Historical alias */
#define LZ4_DISTANCE_ABSOLUTE_MAX 65535

#ifndef LZ4_DISTANCE_MAX
# define LZ4_DISTANCE_MAX LZ4_DISTANCE_ABSOLUTE_MAX
#endif

#define LZ4_ACCELERATION_DEFAULT 1
#define LZ4_ACCELERATION_MAX     65537

#define LZ4_HASHLOG	(LZ4_MEMORY_USAGE - 2)
#define LZ4_HASHTABLESIZE (1 << LZ4_MEMORY_USAGE)
#define LZ4_HASH_SIZE_U32 (1 << LZ4_HASHLOG)

#define LZ4HC_MIN_CLEVEL		0
#define LZ4HC_DEFAULT_CLEVEL		9
#define LZ4HC_MAX_CLEVEL		12
#define LZ4HC_CLEVEL_MIN_COMPRESS	2

#define LZ4HC_DICTIONARY_LOGSIZE 16
#define LZ4HC_MAXD (1 << LZ4HC_DICTIONARY_LOGSIZE)
#define LZ4HC_MAXD_MASK (LZ4HC_MAXD - 1)
#define LZ4HC_HASH_LOG (LZ4HC_DICTIONARY_LOGSIZE - 1)
#define LZ4HC_HASHTABLESIZE (1 << LZ4HC_HASH_LOG)
#define LZ4HC_HASH_MASK (LZ4HC_HASHTABLESIZE - 1)

/*-************************************************************************
 *	STREAMING CONSTANTS AND STRUCTURES
 **************************************************************************/

/*
 * LZ4_stream_t_internal - LZ4 compression state (v1.10.0 layout)
 *
 * Key addition vs old kernel version:
 *   - dictCtx  (pointer to a pre-loaded dictionary context for usingDictCtx)
 *   - tableType (tracks last used table type for Fast Reset)
 */
typedef struct LZ4_stream_t_internal LZ4_stream_t_internal;
struct LZ4_stream_t_internal {
	uint32_t hashTable[LZ4_HASH_SIZE_U32];
	const uint8_t *dictionary;
	const LZ4_stream_t_internal *dictCtx;	/* NEW in v1.9.0 */
	uint32_t currentOffset;
	uint32_t tableType;			/* NEW in v1.9.0 (Fast Reset) */
	uint32_t dictSize;
};

#define LZ4_STREAM_MINSIZE ((1UL << (LZ4_MEMORY_USAGE)) + 32)

typedef union LZ4_stream_u {
	char minStateSize[LZ4_STREAM_MINSIZE];
	LZ4_stream_t_internal internal_donotuse;
} LZ4_stream_t;

/*
 * LZ4_streamHC_t - LZ4 HC compression state.
 */
typedef struct {
	unsigned int hashTable[LZ4HC_HASHTABLESIZE];
	unsigned short chainTable[LZ4HC_MAXD];
	const unsigned char *end;	/* next block to continue on current prefix */
	const unsigned char *base;	/* All index relative to this position */
	const unsigned char *dictBase;	/* alternate base for extDict */
	unsigned int dictLimit;		/* below that point, need extDict */
	unsigned int lowLimit;		/* below that point, no more dict */
	unsigned int nextToUpdate;	/* index from which to continue dict update */
	short compressionLevel;
	/* v1.9.0: favor decompression speed vs compression ratio */
	int8_t favorDecSpeed;
	/* v1.9.0: 1 if currently loading a dict */
	int8_t dirty;
	const struct LZ4HC_CCtx_internal_s *dictCtx;
} LZ4HC_CCtx_internal;

typedef LZ4HC_CCtx_internal LZ4_streamHC_t_internal;

#define LZ4_STREAMHCSIZE        (4 * LZ4HC_HASHTABLESIZE + 2 * LZ4HC_MAXD + 56 + sizeof(void*))
#define LZ4_STREAMHCSIZE_VOIDP (LZ4_STREAMHCSIZE / sizeof(void*))

typedef union LZ4_streamHC_u {
	void* table[LZ4_STREAMHCSIZE_VOIDP];
	LZ4HC_CCtx_internal internal_donotuse;
} LZ4_streamHC_t;

/*
 * LZ4_streamDecode_t - information structure to track an LZ4 stream during decompression.
 */
typedef struct {
	const uint8_t *externalDict;
	const uint8_t *prefixEnd;
	size_t extDictSize;
	size_t prefixSize;
} LZ4_streamDecode_t_internal;

#define LZ4_STREAMDECODESIZE_U64	4
#define LZ4_STREAMDECODESIZE		(LZ4_STREAMDECODESIZE_U64 * sizeof(unsigned long long))

typedef union LZ4_streamDecode_u {
	unsigned long long table[LZ4_STREAMDECODESIZE_U64];
	LZ4_streamDecode_t_internal internal_donotuse;
} LZ4_streamDecode_t;

/*-************************************************************************
 *	SIZE OF STATE
 **************************************************************************/
#define LZ4_MEM_COMPRESS	sizeof(LZ4_stream_t)
#define LZ4HC_MEM_COMPRESS	sizeof(LZ4_streamHC_t)

/*-************************************************************************
 *	Compression Functions
 **************************************************************************/

/**
 * LZ4_compressBound() - Max. output size in worst case szenarios
 * @isize: Size of the input data
 *
 * Return: Max. size LZ4 may output in a "worst case" szenario
 * (data not compressible)
 */
static inline int LZ4_compressBound(size_t isize)
{
	return LZ4_COMPRESSBOUND(isize);
}

/**
 * LZ4_compress_default() - Compress data from source to dest
 * @source: source address of the original data
 * @dest: output buffer address of the compressed data
 * @inputSize: size of the input data. Max supported value is LZ4_MAX_INPUT_SIZE
 * @maxOutputSize: full or partial size of buffer 'dest'
 *	which must be already allocated
 * @wrkmem: address of the working memory.
 *	This requires 'workmem' of LZ4_MEM_COMPRESS.
 *
 * Compresses 'sourceSize' bytes from buffer 'source'
 * into already allocated 'dest' buffer of size 'maxOutputSize'.
 * Compression is guaranteed to succeed if
 * 'maxOutputSize' >= LZ4_compressBound(inputSize).
 * It also runs faster, so it's a recommended setting.
 * If the function cannot compress 'source' into a more limited 'dest' budget,
 * compression stops *immediately*, and the function result is zero.
 * As a consequence, 'dest' content is not valid.
 *
 * Return: Number of bytes written into buffer 'dest'
 *	(necessarily <= maxOutputSize) or 0 if compression fails
 */
int LZ4_compress_default(const char *source, char *dest, int inputSize,
	int maxOutputSize, void *wrkmem);

/**
 * LZ4_compress_fast() - As LZ4_compress_default providing an acceleration param
 * @source: source address of the original data
 * @dest: output buffer address of the compressed data
 * @inputSize: size of the input data. Max supported value is LZ4_MAX_INPUT_SIZE
 * @maxOutputSize: full or partial size of buffer 'dest'
 *	which must be already allocated
 * @acceleration: acceleration factor (1 = default; higher = faster but worse ratio)
 * @wrkmem: address of the working memory.
 *	This requires 'workmem' of LZ4_MEM_COMPRESS.
 *
 * Same as LZ4_compress_default(), but allows to select an "acceleration"
 * factor. The larger the acceleration value, the faster the algorithm,
 * but also the lesser the compression.
 *
 * Return: Number of bytes written into buffer 'dest'
 *	(necessarily <= maxOutputSize) or 0 if compression fails
 */
int LZ4_compress_fast(const char *source, char *dest, int inputSize,
	int maxOutputSize, int acceleration, void *wrkmem);

/**
 * LZ4_compress_destSize() - Compress as much data as possible from source to dest
 * @source: source address of the original data
 * @dest: output buffer address of the compressed data
 * @sourceSizePtr: will be modified to indicate how many bytes where read
 *	from 'source' to fill 'dest'. New value is necessarily <= old value.
 * @targetDestSize: Size of buffer 'dest' which must be already allocated
 * @wrkmem: address of the working memory.
 *	This requires 'workmem' of LZ4_MEM_COMPRESS.
 *
 * Reverse the logic, by compressing as much data as possible from 'source'
 * buffer into already allocated buffer 'dest' of size 'targetDestSize'.
 *
 * Return: Number of bytes written into 'dest' (necessarily <= targetDestSize)
 *	or 0 if compression fails
 */
int LZ4_compress_destSize(const char *source, char *dest, int *sourceSizePtr,
	int targetDestSize, void *wrkmem);

/*-************************************************************************
 *	Decompression Functions
 **************************************************************************/

/**
 * LZ4_decompress_fast() - Decompresses data from 'source' into 'dest'
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 *	which must be already allocated with 'originalSize' bytes
 * @originalSize: is the original and therefore uncompressed size
 *
 * Decompresses data from 'source' into 'dest'.
 * This function fully respects memory boundaries for properly formed
 * compressed data.
 * It is a bit faster than LZ4_decompress_safe().
 * However, it does not provide any protection against intentionally
 * modified data stream (malicious input).
 * Use this function in trusted environment only
 * (data to decode comes from a trusted source).
 *
 * Return: number of bytes read from the source buffer
 *	or a negative result if decompression fails.
 */
int LZ4_decompress_fast(const char *source, char *dest, int originalSize);

/**
 * LZ4_decompress_safe() - Decompression protected against buffer overflow
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 *	which must be already allocated
 * @compressedSize: is the precise full size of the compressed block
 * @maxDecompressedSize: is the size of 'dest' buffer
 *
 * Decompresses data from 'source' into 'dest'.
 * If the source stream is detected malformed, the function will
 * stop decoding and return a negative result.
 * This function is protected against buffer overflow exploits,
 * including malicious data packets. It never writes outside output buffer,
 * nor reads outside input buffer.
 *
 * Return: number of bytes decompressed into destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_safe(const char *source, char *dest, int compressedSize,
	int maxDecompressedSize);

/**
 * LZ4_decompress_safe_partial() - Decompress a block of size 'compressedSize'
 *	at position 'source' into buffer 'dest'
 * @source: source address of the compressed data
 * @dest: output buffer address of the decompressed data which must be
 *	already allocated
 * @compressedSize: is the precise full size of the compressed block.
 * @targetOutputSize: the decompression operation will try
 *	to stop as soon as 'targetOutputSize' has been reached
 * @maxDecompressedSize: is the size of destination buffer
 *
 * This function decompresses a compressed block of size 'compressedSize'
 * at position 'source' into destination buffer 'dest'
 * of size 'maxDecompressedSize'.
 * The function tries to stop decompressing operation as soon as
 * 'targetOutputSize' has been reached, reducing decompression time.
 *
 * Return: the number of bytes decoded in the destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_safe_partial(const char *source, char *dest,
	int compressedSize, int targetOutputSize, int maxDecompressedSize);

/*-************************************************************************
 *	LZ4 HC Compression
 **************************************************************************/

/**
 * LZ4_compress_HC() - Compress data from `src` into `dst`, using HC algorithm
 * @src: source address of the original data
 * @dst: output buffer address of the compressed data
 * @srcSize: size of the input data. Max supported value is LZ4_MAX_INPUT_SIZE
 * @dstCapacity: full or partial size of buffer 'dst',
 *	which must be already allocated
 * @compressionLevel: Recommended values are between 4 and 9, although any
 *	value between 1 and LZ4HC_MAX_CLEVEL will work.
 *	Values >LZ4HC_MAX_CLEVEL behave the same as 12.
 * @wrkmem: address of the working memory.
 *	This requires 'wrkmem' of size LZ4HC_MEM_COMPRESS.
 *
 * Compress data from 'src' into 'dst', using the more powerful
 * but slower "HC" algorithm. Compression is guaranteed to succeed if
 * `dstCapacity >= LZ4_compressBound(srcSize)
 *
 * Return : the number of bytes written into 'dst' or 0 if compression fails.
 */
int LZ4_compress_HC(const char *src, char *dst, int srcSize, int dstCapacity,
	int compressionLevel, void *wrkmem);

/**
 * LZ4_resetStreamHC() - Init an allocated 'LZ4_streamHC_t' structure
 * @streamHCPtr: pointer to the 'LZ4_streamHC_t' structure
 * @compressionLevel: compression level (between LZ4HC_MIN_CLEVEL and LZ4HC_MAX_CLEVEL)
 */
void LZ4_resetStreamHC(LZ4_streamHC_t *streamHCPtr, int compressionLevel);

/**
 * LZ4_loadDictHC() - Load a static dictionary into LZ4_streamHC
 * @streamHCPtr: pointer to the LZ4HC_stream_t
 * @dictionary: dictionary to load
 * @dictSize: size of dictionary
 *
 * Return : dictionary size, in bytes (necessarily <= 64 KB)
 */
int LZ4_loadDictHC(LZ4_streamHC_t *streamHCPtr, const char *dictionary,
	int dictSize);

/**
 * LZ4_compress_HC_continue() - Compress 'src' using data from previously
 *	compressed blocks as a dictionary using the HC algorithm
 * @streamHCPtr: Pointer to the previous 'LZ4_streamHC_t' structure
 * @src: source address of the original data
 * @dst: output buffer address of the compressed data
 * @srcSize: size of the input data
 * @maxDstSize: full or partial size of buffer 'dest'
 *
 * Return: Number of bytes written into buffer 'dst' or 0 if compression fails
 */
int LZ4_compress_HC_continue(LZ4_streamHC_t *streamHCPtr, const char *src,
	char *dst, int srcSize, int maxDstSize);

/**
 * LZ4_saveDictHC() - Save static dictionary from LZ4HC_stream
 * @streamHCPtr: pointer to the 'LZ4HC_stream_t' structure
 * @safeBuffer: buffer to save dictionary to, must be already allocated
 * @maxDictSize: size of 'safeBuffer'
 *
 * Return : saved dictionary size in bytes (necessarily <= maxDictSize),
 *	or 0 if error.
 */
int LZ4_saveDictHC(LZ4_streamHC_t *streamHCPtr, char *safeBuffer,
	int maxDictSize);

/*-*********************************************
 *	Streaming Compression Functions
 ***********************************************/

/**
 * LZ4_resetStream() - Init an allocated 'LZ4_stream_t' structure
 * @LZ4_stream: pointer to the 'LZ4_stream_t' structure
 */
void LZ4_resetStream(LZ4_stream_t *LZ4_stream);

/**
 * LZ4_resetStream_fast() - Faster init of 'LZ4_stream_t' (v1.9.0+)
 * @ctx: pointer to the 'LZ4_stream_t' structure
 *
 * Use this to prepare an LZ4_stream_t for a new chain of dependent blocks
 * when the state was already properly initialized before (e.g. by
 * LZ4_resetStream). Avoids a full memset of the hash table.
 */
void LZ4_resetStream_fast(LZ4_stream_t *ctx);

/**
 * LZ4_loadDict() - Load a static dictionary into LZ4_stream
 * @streamPtr: pointer to the LZ4_stream_t
 * @dictionary: dictionary to load
 * @dictSize: size of dictionary
 *
 * Return : dictionary size, in bytes (necessarily <= 64 KB)
 */
int LZ4_loadDict(LZ4_stream_t *streamPtr, const char *dictionary,
	int dictSize);

/**
 * LZ4_saveDict() - Save static dictionary from LZ4_stream
 * @streamPtr: pointer to the 'LZ4_stream_t' structure
 * @safeBuffer: buffer to save dictionary to, must be already allocated
 * @dictSize: size of 'safeBuffer'
 *
 * Return : saved dictionary size in bytes (necessarily <= dictSize),
 *	or 0 if error.
 */
int LZ4_saveDict(LZ4_stream_t *streamPtr, char *safeBuffer, int dictSize);

/**
 * LZ4_compress_fast_continue() - Compress 'src' using data from previously
 *	compressed blocks as a dictionary
 * @streamPtr: Pointer to the previous 'LZ4_stream_t' structure
 * @src: source address of the original data
 * @dst: output buffer address of the compressed data
 * @srcSize: size of the input data
 * @maxDstSize: full or partial size of buffer 'dest'
 * @acceleration: acceleration factor
 *
 * Return: Number of bytes written into buffer 'dst' or 0 if compression fails
 */
int LZ4_compress_fast_continue(LZ4_stream_t *streamPtr, const char *src,
	char *dst, int srcSize, int maxDstSize, int acceleration);

/**
 * LZ4_setStreamDecode() - Instruct where to find dictionary
 * @LZ4_streamDecode: the 'LZ4_streamDecode_t' structure
 * @dictionary: dictionary to use
 * @dictSize: size of dictionary
 *
 * Return: 1 if OK, 0 if error
 */
int LZ4_setStreamDecode(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *dictionary, int dictSize);

/**
 * LZ4_decompress_safe_continue() - Decompress blocks in streaming mode
 * @LZ4_streamDecode: the 'LZ4_streamDecode_t' structure
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 * @compressedSize: is the precise full size of the compressed block
 * @maxDecompressedSize: is the size of 'dest' buffer
 *
 * Return: number of bytes decompressed into destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_safe_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int compressedSize,
	int maxDecompressedSize);

/**
 * LZ4_decompress_fast_continue() - Decompress blocks in streaming mode
 * @LZ4_streamDecode: the 'LZ4_streamDecode_t' structure
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 * @originalSize: is the original and therefore uncompressed size
 *
 * Return: number of bytes decompressed into destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_fast_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int originalSize);

/**
 * LZ4_decompress_safe_usingDict() - Same as LZ4_setStreamDecode()
 *	followed by LZ4_decompress_safe_continue()
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 * @compressedSize: is the precise full size of the compressed block
 * @maxDecompressedSize: is the size of 'dest' buffer
 * @dictStart: pointer to the start of the dictionary in memory
 * @dictSize: size of dictionary
 *
 * Return: number of bytes decompressed into destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_safe_usingDict(const char *source, char *dest,
	int compressedSize, int maxDecompressedSize, const char *dictStart,
	int dictSize);

/**
 * LZ4_decompress_fast_usingDict() - Same as LZ4_setStreamDecode()
 *	followed by LZ4_decompress_fast_continue()
 * @source: source address of the compressed data
 * @dest: output buffer address of the uncompressed data
 * @originalSize: is the original and therefore uncompressed size
 * @dictStart: pointer to the start of the dictionary in memory
 * @dictSize: size of dictionary
 *
 * Return: number of bytes decompressed into destination buffer
 *	(necessarily <= maxDecompressedSize)
 *	or a negative result in case of error
 */
int LZ4_decompress_fast_usingDict(const char *source, char *dest,
	int originalSize, const char *dictStart, int dictSize);

#define LZ4_DECOMPRESS_INPLACE_MARGIN(compressedSize)          (((compressedSize) >> 8) + 32)
#define LZ4_DECOMPRESS_INPLACE_BUFFER_SIZE(decompressedSize)   ((decompressedSize) + LZ4_DECOMPRESS_INPLACE_MARGIN(decompressedSize))

#ifndef LZ4_DISTANCE_MAX
# define LZ4_DISTANCE_MAX 65535	/* set to maximum value by default */
#endif

#endif /* __LZ4_H__ */
