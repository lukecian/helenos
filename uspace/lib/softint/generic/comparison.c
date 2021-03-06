/*
 * Copyright (c) 2011 Petr Koupy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup softint
 * @{
 */
/**
 * @file Signed and unsigned comparisons.
 */

#include <alias.h>
#include <comparison.h>
#include <lltype.h>

#define LESSER  0;
#define EQUAL   1;
#define GREATER 2;

int __cmpti2 (long long a, long long b)
{
	union lltype lla;
	union lltype llb;

	lla.s_whole = a;
	llb.s_whole = b;

	if (lla.s_half[HI] < llb.s_half[HI]) {
		return LESSER;
	} else if (lla.s_half[HI] > llb.s_half[HI]) {
		return GREATER;
	} else {
		if (lla.u_half[LO] < llb.u_half[LO]) {
			return LESSER;
		} else if (lla.u_half[LO] > llb.u_half[LO]) {
			return GREATER;
		} else {
			return EQUAL;
		}
	}
}

int __ucmpti2 (unsigned long long a, unsigned long long b)
{
	union lltype lla;
	union lltype llb;

	lla.u_whole = a;
	llb.u_whole = b;

	if (lla.u_half[HI] < llb.u_half[HI]) {
		return LESSER;
	} else if (lla.u_half[HI] > llb.u_half[HI]) {
		return GREATER;
	} else {
		if (lla.u_half[LO] < llb.u_half[LO]) {
			return LESSER;
		} else if (lla.u_half[LO] > llb.u_half[LO]) {
			return GREATER;
		} else {
			return EQUAL;
		}
	}
}

#if LONG_MAX == LLONG_MAX
int ALIAS(__cmp, i2);
int ALIAS(__ucmp, i2);
#else

int __cmpdi2(long a, long b)
{
	if ((int)a < (int)b) {
		return LESSER;
	} else if ((int)a > (int)b) {
		return GREATER;
	} else {
		return EQUAL;
	}
}

int __ucmpdi2(unsigned long a, unsigned long b)
{
	if ((int)a < (int)b) {
		return LESSER;
	} else if ((int)a > (int)b) {
		return GREATER;
	} else {
		return EQUAL;
	}
}

#endif

/** @}
 */
