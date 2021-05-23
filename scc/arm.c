#include <stdbool.h>
#include "../bits.h"


static bool modified_immediate(unsigned long long x)
{
	if (x == 0)
		return true;

	// move constant at right
	x = ror(x, ctz(x));
	// and test that the 24 MSBs ar zero
	return (x & ~0xFF) == 0;
}
