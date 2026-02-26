#ifndef _ASM_ARM64_ARCH_HWEIGHT_H_
#define _ASM_ARM64_ARCH_HWEIGHT_H_

#include <asm/types.h>

static __always_inline unsigned int __arch_hweight32(unsigned int w)
{
	return __builtin_popcount(w);
}

static __always_inline unsigned int __arch_hweight16(unsigned int w)
{
	return __builtin_popcount(w & 0xffff);
}

static __always_inline unsigned int __arch_hweight8(unsigned int w)
{
	return __builtin_popcount(w & 0xff);
}

static __always_inline unsigned long __arch_hweight64(__u64 w)
{
	return __builtin_popcountll(w);
}

#endif
