#ifndef _ASM_GENERIC_BITOPS_FIND_H_
#define _ASM_GENERIC_BITOPS_FIND_H_

extern unsigned long _find_next_bit(const unsigned long *addr,
		unsigned long nbits, unsigned long start, unsigned long invert);

#ifndef find_next_bit
/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
extern unsigned long find_next_bit(const unsigned long *addr, unsigned long
		size, unsigned long offset);

static inline unsigned long __inline_find_next_bit(const unsigned long *addr,
						   unsigned long size,
						   unsigned long offset)
{
	if (__builtin_constant_p(size) && size <= BITS_PER_LONG) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *(const unsigned long *)addr & (~0UL << offset);
		if (val) {
			unsigned long res = __ffs(val);
			return res < size ? res : size;
		}
		return size;
	}

	return _find_next_bit(addr, size, offset, 0UL);
}

#define find_next_bit(addr, size, offset) __inline_find_next_bit((const unsigned long *)(addr), (size), (offset))

#endif

#ifndef find_next_zero_bit
/**
 * find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number of the next zero bit
 * If no bits are zero, returns @size.
 */
extern unsigned long find_next_zero_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset);

static inline unsigned long __inline_find_next_zero_bit(const unsigned long *addr,
							unsigned long size,
							unsigned long offset)
{
	if (__builtin_constant_p(size) && size <= BITS_PER_LONG) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = ~(*(const unsigned long *)addr) & (~0UL << offset);
		if (val) {
			unsigned long res = __ffs(val);
			return res < size ? res : size;
		}
		return size;
	}

	return _find_next_bit(addr, size, offset, ~0UL);
}

#define find_next_zero_bit(addr, size, offset) __inline_find_next_zero_bit((const unsigned long *)(addr), (size), (offset))

#endif

#ifdef CONFIG_GENERIC_FIND_FIRST_BIT

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 *
 * Returns the bit number of the first set bit.
 * If no bits are set, returns @size.
 */
extern unsigned long find_first_bit(const unsigned long *addr,
				    unsigned long size);

/**
 * find_first_zero_bit - find the first cleared bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 *
 * Returns the bit number of the first cleared bit.
 * If no bits are zero, returns @size.
 */
extern unsigned long find_first_zero_bit(const unsigned long *addr,
					 unsigned long size);
#else /* CONFIG_GENERIC_FIND_FIRST_BIT */

#define find_first_bit(addr, size) find_next_bit((addr), (size), 0)
#define find_first_zero_bit(addr, size) find_next_zero_bit((addr), (size), 0)

#endif /* CONFIG_GENERIC_FIND_FIRST_BIT */

#endif /*_ASM_GENERIC_BITOPS_FIND_H_ */
