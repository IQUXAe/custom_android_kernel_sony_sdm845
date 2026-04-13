#ifndef _ASM_ARM64_BITOPS_FIND_H_
#define _ASM_ARM64_BITOPS_FIND_H_

static __always_inline unsigned long
_find_next_bit_arm64(const unsigned long *addr, unsigned long size,
		     unsigned long offset, unsigned long invert)
{
	const unsigned long *p;
	unsigned long result;
	unsigned long tmp;

	if (unlikely(offset >= size))
		return size;

	result = offset & ~(BITS_PER_LONG - 1);
	p = addr + BIT_WORD(offset);
	tmp = (*p ^ invert) & (~0UL << (offset & (BITS_PER_LONG - 1)));

	for (;;) {
		if (tmp) {
			unsigned long bit = result + __ffs(tmp);

			return bit < size ? bit : size;
		}

		result += BITS_PER_LONG;
		if (result >= size)
			return size;

		tmp = *++p ^ invert;
	}
}

#ifndef find_next_bit
static __always_inline unsigned long
__inline_find_next_bit_arm64(const unsigned long *addr, unsigned long size,
			     unsigned long offset)
{
	return _find_next_bit_arm64(addr, size, offset, 0UL);
}

#define find_next_bit(addr, size, offset) \
	__inline_find_next_bit_arm64((const unsigned long *)(addr), \
				     (size), (offset))
#endif

#ifndef find_next_zero_bit
static __always_inline unsigned long
__inline_find_next_zero_bit_arm64(const unsigned long *addr,
				  unsigned long size,
				  unsigned long offset)
{
	return _find_next_bit_arm64(addr, size, offset, ~0UL);
}

#define find_next_zero_bit(addr, size, offset) \
	__inline_find_next_zero_bit_arm64((const unsigned long *)(addr), \
					  (size), (offset))
#endif

#ifndef find_first_bit
#define find_first_bit(addr, size) find_next_bit((addr), (size), 0)
#endif

#ifndef find_first_zero_bit
#define find_first_zero_bit(addr, size) find_next_zero_bit((addr), (size), 0)
#endif

#endif /* _ASM_ARM64_BITOPS_FIND_H_ */
