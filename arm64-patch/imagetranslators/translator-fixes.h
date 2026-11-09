/*
 * Forced into every translator compiled for RenkuOS arm64 (r1~beta6
 * hrev60071_15). Two problems of that system, worked around here:
 *
 * 1. libroot's arm64 __swap_float/__swap_double (os/arch/arm64/byteorder.S)
 *    have their fmov operands reversed: they overwrite the argument with the
 *    still-unswapped integer register and return garbage. Every translator
 *    writes the B_TRANSLATOR_BITMAP header's bounds with
 *    B_HOST_TO_BENDIAN_FLOAT, so BBitmapStream built a bitmap of a random
 *    size and the copy of the first rows overran it -- Tracker crashed
 *    drawing a JPEG desktop background, ShowImage crashed opening one.
 *    Calls are renamed to the correct versions below.
 *
 * 2. ICO and RAW use B_TRANSLATOR_EXT_DOCUMENT_COUNT/INDEX, which
 *    libtranslation only exports from a later Haiku (13a583a7f0).
 */
#ifndef RENKU_TRANSLATOR_FIXES_H
#define RENKU_TRANSLATOR_FIXES_H

#define __swap_float __renku_swap_float
#define __swap_double __renku_swap_double

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((weak, visibility("hidden"))) float
__renku_swap_float(float value)
{
	unsigned int bits;
	__builtin_memcpy(&bits, &value, sizeof(bits));
	bits = __builtin_bswap32(bits);
	__builtin_memcpy(&value, &bits, sizeof(bits));
	return value;
}

__attribute__((weak, visibility("hidden"))) double
__renku_swap_double(double value)
{
	unsigned long long bits;
	__builtin_memcpy(&bits, &value, sizeof(bits));
	bits = __builtin_bswap64(bits);
	__builtin_memcpy(&value, &bits, sizeof(bits));
	return value;
}

__attribute__((weak, visibility("hidden"))) char
	B_TRANSLATOR_EXT_DOCUMENT_COUNT[] = "/documentCount";
__attribute__((weak, visibility("hidden"))) char
	B_TRANSLATOR_EXT_DOCUMENT_INDEX[] = "/documentIndex";

#ifdef __cplusplus
}
#endif

#endif
