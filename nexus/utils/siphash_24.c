/* \file siphash_24.c
 * \brief Siphash Reference C implementation
 * \author Angaza
 * \copyright 2020 Angaza, Inc.
 * \license This file is released under the MIT license
 *
 * The above copyright notice and license shall be included in all copies
 * or substantial portions of the Software.
 *
 * Based on original source from:
 * Jean-Philippe Aumasson <jeanphilippe.aumasson@gmail.com>
 * Daniel J. Bernstein <djb@cr.yp.to>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * Original unmodified Siphash implementation was distributed under
 * CC0 Public Domain license:
 * See <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "siphash_24.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

static u64 v0;
static u64 v1;
static u64 v2;
static u64 v3;

#define ROTL(x, b) (u64)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v)                                                        \
    (p)[0] = (u8)((v));                                                        \
    (p)[1] = (u8)((v) >> 8);                                                   \
    (p)[2] = (u8)((v) >> 16);                                                  \
    (p)[3] = (u8)((v) >> 24);

#define U64TO8_LE(p, v)                                                        \
    U32TO8_LE((p), (u32)((v)));                                                \
    U32TO8_LE((p) + 4, (u32)((v) >> 32));

#define U8TO64_LE(p)                                                           \
    (((u64)((p)[0])) | ((u64)((p)[1]) << 8) | ((u64)((p)[2]) << 16) |          \
     ((u64)((p)[3]) << 24) | ((u64)((p)[4]) << 32) | ((u64)((p)[5]) << 40) |   \
     ((u64)((p)[6]) << 48) | ((u64)((p)[7]) << 56))

static void SIPROUND(void)
{
    do
    {
        v0 += v1;
        v1 = ROTL(v1, 13);
        v1 ^= v0;
        v0 = ROTL(v0, 32);
        v2 += v3;
        v3 = ROTL(v3, 16);
        v3 ^= v2;
        v0 += v3;
        v3 = ROTL(v3, 21);
        v3 ^= v0;
        v2 += v1;
        v1 = ROTL(v1, 17);
        v1 ^= v2;
        v2 = ROTL(v2, 32);
    } while (0);
}

/* SipHash-2-4 */
void siphash24_compute(uint8_t* out,
                       const uint8_t* in,
                       const uint32_t inlen,
                       const uint8_t* k)
{
    const u8* end = in + inlen - (inlen % sizeof(u64));
    const int left = inlen & 7;
    u64 b = ((u64) inlen) << 56;

    /* "somepseudorandomlygeneratedbytes" */
    v0 = 0x736f6d6570736575ULL;
    v1 = 0x646f72616e646f6dULL;
    v2 = 0x6c7967656e657261ULL;
    v3 = 0x7465646279746573ULL;
    const u64 k0 = U8TO64_LE(k);
    const u64 k1 = U8TO64_LE(k + 8);

    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    for (; in != end; in += 8)
    {
        const u64 m = U8TO64_LE(in);
        v3 ^= m;
        SIPROUND();
        SIPROUND();
        v0 ^= m;
    }

    switch (left)
    {
        case 7:
            b |= ((u64) in[6]) << 48;
            // intentional fallthrough

        case 6:
            b |= ((u64) in[5]) << 40;
            // intentional fallthrough

        case 5:
            b |= ((u64) in[4]) << 32;
            // intentional fallthrough

        case 4:
            b |= ((u64) in[3]) << 24;
            // intentional fallthrough

        case 3:
            b |= ((u64) in[2]) << 16;
            // intentional fallthrough

        case 2:
            b |= ((u64) in[1]) << 8;
            // intentional fallthrough

        case 1:
            b |= ((u64) in[0]);
            break;

        case 0:
            break;

        default:
            // should never arrive here, invalid check calculation result
            assert(false);
    }
    v3 ^= b;
    SIPROUND();
    SIPROUND();
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND();
    SIPROUND();
    SIPROUND();
    SIPROUND();
    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);
}
