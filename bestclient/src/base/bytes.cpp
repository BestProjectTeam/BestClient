/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "bytes.h"

void swap_endian(void *data, unsigned elem_size, unsigned num)
{
	char *src = (char *)data;
	char *dst = src + (elem_size - 1);

	while(num)
	{
		unsigned n = elem_size >> 1;
		char tmp;
		while(n)
		{
			tmp = *src;
			*src = *dst;
			*dst = tmp;

			src++;
			dst--;
			n--;
		}

		src = src + (elem_size >> 1);
		dst = src + (elem_size - 1);
		num--;
	}
}

static_assert(sizeof(unsigned) == 4, "unsigned must be 4 bytes in size");
static_assert(sizeof(unsigned) == sizeof(int), "unsigned and int must have the same size");

unsigned bytes_be_to_uint(const unsigned char *bytes)
{
	return ((bytes[0] & 0xffu) << 24u) | ((bytes[1] & 0xffu) << 16u) | ((bytes[2] & 0xffu) << 8u) | (bytes[3] & 0xffu);
}

void uint_to_bytes_be(unsigned char *bytes, unsigned value)
{
	bytes[0] = (value >> 24u) & 0xffu;
	bytes[1] = (value >> 16u) & 0xffu;
	bytes[2] = (value >> 8u) & 0xffu;
	bytes[3] = value & 0xffu;
}
