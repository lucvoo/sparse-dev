#include <stdbool.h>
#include "../bits.h"


static bool bitmask(unsigned long long x, unsigned size)
{
	unsigned long long mask = bits_mask(size);
	unsigned int n;


	// If valid it's negation is valid too
	// -> test the one with 0 at lsb.
	if (x & 1)
		x = ~x;

	// all 0s (or all 1s) is never valid
	x &= mask;
	if (!x)
		return false;

	// collapse the repeated pattern
	while (size > 2) {
		unsigned long long hi, lo;

		size /= 2;
		mask >>= size;

		hi = x >> size;
		lo = x & mask;
		if (hi != lo)
			break;
		x = lo;
	}

	// x will never be all 0s or all 1s.
	// Test if there is exactly 2 transitions
	n = __builtin_ctzll(x);
	x >>= n;
	n = __builtin_ctzll(~x);
	x >>= n;
	return x == 0;
}

static bool inverted_immediate(unsigned long long x)
{
	unsigned long long mask = 0xffff;
	unsigned int size = 64;

	while (size) {
		if ((~x & ~mask) == 0)
			return true;
		mask <<= 16;
		size -= 16;
	}
	return false;
}

static bool wide_immediate(unsigned long long x)
{
	unsigned long long mask = 0xffff;

	if (!(x & ~mask) || !(~x & ~mask))
		return true;
	mask <<= 16;
	if (!(x & ~mask) || !(~x & ~mask))
		return true;
	mask <<= 16;
	if (!(x & ~mask) || !(~x & ~mask))
		return true;
	mask <<= 16;
	if (!(x & ~mask) || !(~x & ~mask))
		return true;
	return false;
}
