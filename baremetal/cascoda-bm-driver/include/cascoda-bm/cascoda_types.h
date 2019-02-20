/**
 * @file cascoda_types.h
 * @brief Type definitions used by Cascoda baremetal drivers
 *//*
 * Copyright (C) 2016  Cascoda, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
/***************************************************************************/ /**
 * \def LS_BYTE(x)
 * Extract the least significant octet of a 16-bit value
 * \def MS_BYTE(x)
 * Extract the most significant octet of a 16-bit value
 * \def LS0_BYTE(x)
 * Extract the first (little-endian) octet of a 32-bit value
 * \def LS1_BYTE(x)
 * Extract the second (little-endian) octet of a 32-bit value
 * \def LS2_BYTE(x)
 * Extract the third (little-endian) octet of a 32-bit value
 * \def LS3_BYTE(x)
 * Extract the fourth (little-endian) octet of a 32-bit value
 * \def GETLE16(x)
 * Extract a 16-bit value from a little-endian octet array
 * \def GETLE32(x)
 * Extract a 32-bit value from a little-endian octet array
 * \def PUTLE16(x,y)
 * Put a 16-bit value x into a little-endian octet array y
 * \def PUTLE32(x,y)
 * Put a 32-bit value x into a little-endian octet array y
 ******************************************************************************/
#ifndef CASCODA_TYPES_H
#define CASCODA_TYPES_H

#include <stddef.h>
#include <stdint.h>

/***************************************************************************/ /**
 * \defgroup FWidths Fixed width types
 ************************************************************************** @{*/
#if __STDC_VERSION__ < 199901L

typedef unsigned char      u8_t;
typedef signed char        i8_t;
typedef unsigned short     u16_t;
typedef signed short       i16_t;
typedef unsigned long      u32_t;
typedef signed long        i32_t;
typedef unsigned long long u64_t;
typedef signed long long   i64_t;

#else

typedef uint8_t  u8_t;
typedef int8_t   i8_t;
typedef uint16_t u16_t;
typedef int16_t  i16_t;
typedef uint32_t u32_t;
typedef int32_t  i32_t;
typedef uint64_t u64_t;
typedef int64_t  i64_t;

#endif

/**@}*/

#define NULLP 0 /**< NULL packet */

/******************************************************************************/
/****** Macros                                                           ******/
/******************************************************************************/
#ifndef _CASCODA_MACROS
#define _CASCODA_MACROS

#define LS_BYTE(x) ((u8_t)((x)&0xFF))
#define MS_BYTE(x) ((u8_t)(((x) >> 8) & 0xFF))
#define LS0_BYTE(x) ((u8_t)((x)&0xFF))
#define LS1_BYTE(x) ((u8_t)(((x) >> 8) & 0xFF))
#define LS2_BYTE(x) ((u8_t)(((x) >> 16) & 0xFF))
#define LS3_BYTE(x) ((u8_t)(((x) >> 24) & 0xFF))

#define GETLE16(x) (((u16_t)(x)[1] << 8) + (x)[0])
#define GETLE32(x) (((u32_t)(x)[3] << 24) + ((u32_t)(x)[2] << 16) + ((u32_t)(x)[1] << 8) + (x)[0])
#define PUTLE16(x, y)        \
	{                        \
		(y)[0] = ((x)&0xff); \
		(y)[1] = ((x) >> 8); \
	}
#define PUTLE32(x, y)                  \
	{                                  \
		(y)[0] = ((x)&0xff);           \
		(y)[1] = (((x) >> 8) & 0xff);  \
		(y)[2] = (((x) >> 16) & 0xff); \
		(y)[3] = (((x) >> 24) & 0xff); \
	}

#endif

#endif // CASCODA_TYPES_H
