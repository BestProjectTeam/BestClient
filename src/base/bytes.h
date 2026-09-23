/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef BASE_BYTES_H
#define BASE_BYTES_H

/**
 * Byte-manipulation utilities.
 *
 * @defgroup Bytes Bytes
 */

/**
 * Swaps the endianness of data. Each element is swapped individually by reversing its bytes.
 *
 * @ingroup Bytes
 *
 * @param data Pointer to data to be swapped.
 * @param elem_size Size in bytes of each element.
 * @param num Number of elements.
 *
 * @remark The caller must ensure that the data is at least `elem_size * num` bytes in size.
 */
void swap_endian(void *data, unsigned elem_size, unsigned num);

/**
 * Packs 4 big endian bytes into an unsigned.
 *
 * @ingroup Bytes
 *
 * @param bytes Pointer to an array of bytes that will be packed.
 *
 * @return The packed unsigned.
 *
 * @remark Assumes the passed array is least 4 bytes in size.
 * @remark Assumes unsigned is 4 bytes in size.
 *
 * @see uint_to_bytes_be
 */
unsigned bytes_be_to_uint(const unsigned char *bytes);

/**
 * Packs an unsigned into 4 big endian bytes.
 *
 * @ingroup Bytes
 *
 * @param bytes Pointer to an array where the bytes will be stored.
 * @param value The values that will be packed into the array.
 *
 * @remark Assumes the passed array is least 4 bytes in size.
 * @remark Assumes unsigned is 4 bytes in size.
 *
 * @see bytes_be_to_uint
 */
void uint_to_bytes_be(unsigned char *bytes, unsigned value);

#endif
