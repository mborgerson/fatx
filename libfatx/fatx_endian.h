/*
 * FATX Filesystem Library
 *
 * Copyright (C) 2015  Matt Borgerson
 * Copyright (C) 2026  Mijael Viricochea
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FATX_ENDIAN_H
#define FATX_ENDIAN_H

#include <stdint.h>

#include "fatx.h"

/*
 * On-disk byte order.
 *
 * The original Xbox stores all multi-byte on-disk values little-endian; the
 * Xbox 360 stores the same structures big-endian. Every multi-byte value that
 * comes from, or is going to, the disk must pass through one of the helpers
 * below. Raw file data must NOT: it is a byte stream and has no byte order.
 *
 * The conversion is symmetric, so from_disk and to_disk are the same operation.
 * They exist as separate names purely so call sites document their direction.
 */

/* Byte order of the host we were compiled for. */
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define FATX_HOST_BIG_ENDIAN 1
#else
#define FATX_HOST_BIG_ENDIAN 0
#endif

#if defined(_MSC_VER)
#include <stdlib.h>
#define FATX_BSWAP16(v) _byteswap_ushort(v)
#define FATX_BSWAP32(v) _byteswap_ulong(v)
#define FATX_BSWAP64(v) _byteswap_uint64(v)
#else
#define FATX_BSWAP16(v) __builtin_bswap16(v)
#define FATX_BSWAP32(v) __builtin_bswap32(v)
#define FATX_BSWAP64(v) __builtin_bswap64(v)
#endif

/*
 * True when the on-disk byte order differs from the host's, i.e. when values
 * crossing the disk boundary have to be swapped.
 */
static inline int fatx_endian_swap_required(struct fatx_fs const *fs)
{
    int disk_is_big_endian = (fs->variant == FATX_VARIANT_X360);
    return disk_is_big_endian != FATX_HOST_BIG_ENDIAN;
}

static inline uint16_t fatx_from_disk_u16(struct fatx_fs const *fs, uint16_t value)
{
    return fatx_endian_swap_required(fs) ? FATX_BSWAP16(value) : value;
}

static inline uint32_t fatx_from_disk_u32(struct fatx_fs const *fs, uint32_t value)
{
    return fatx_endian_swap_required(fs) ? FATX_BSWAP32(value) : value;
}

static inline uint64_t fatx_from_disk_u64(struct fatx_fs const *fs, uint64_t value)
{
    return fatx_endian_swap_required(fs) ? FATX_BSWAP64(value) : value;
}

static inline uint16_t fatx_to_disk_u16(struct fatx_fs const *fs, uint16_t value)
{
    return fatx_from_disk_u16(fs, value);
}

static inline uint32_t fatx_to_disk_u32(struct fatx_fs const *fs, uint32_t value)
{
    return fatx_from_disk_u32(fs, value);
}

static inline uint64_t fatx_to_disk_u64(struct fatx_fs const *fs, uint64_t value)
{
    return fatx_from_disk_u64(fs, value);
}

/*
 * Identify the variant from a raw, unswapped signature word.
 *
 * The signature is byte-identical in both variants: the original Xbox writes
 * the bytes 'F','A','T','X' and reads them little-endian, the Xbox 360 writes
 * 'X','T','A','F' and reads them big-endian, and both yield FATX_SIGNATURE.
 * So whichever way round the raw word matches tells us the disk's byte order.
 *
 * Returns FATX_VARIANT_AUTO if the word is not a FATX signature either way.
 */
static inline enum fatx_variant fatx_variant_from_raw_signature(uint32_t raw,
                                                                uint32_t signature)
{
    if (raw == signature)
    {
        /* Disk byte order matches the host's. */
        return FATX_HOST_BIG_ENDIAN ? FATX_VARIANT_X360 : FATX_VARIANT_XBOX;
    }

    if (FATX_BSWAP32(raw) == signature)
    {
        /* Disk byte order is the opposite of the host's. */
        return FATX_HOST_BIG_ENDIAN ? FATX_VARIANT_XBOX : FATX_VARIANT_X360;
    }

    return FATX_VARIANT_AUTO;
}

static inline char const *fatx_variant_name(enum fatx_variant variant)
{
    switch (variant)
    {
        case FATX_VARIANT_XBOX: return "xbox";
        case FATX_VARIANT_X360: return "x360";
        default:                return "auto";
    }
}

#endif
