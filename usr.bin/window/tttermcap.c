/*	$NetBSD: tttermcap.c,v 1.9 2009/04/14 08:50:06 lukem Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Wang at The University of California, Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)tttermcap.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: tttermcap.c,v 1.9 2009/04/14 08:50:06 lukem Exp $");
#endif
#endif /* not lint */

#include <stdlib.h>
#include <string.h>
#include <termcap.h>
#include "tt.h"

int
tttputc(int c)
{
	ttputc(c);
	return (0);
}

int
ttxputc(int c)
{
	*tt_strp++ = c;
	return (0);
}

struct tt_str *
tttgetstr(const char *str)
{
	struct tt_str *s;

	if ((str = tgetstr(str, &tt_strp)) == 0)
		return 0;
	if ((s = (struct tt_str *) malloc(sizeof *s)) == 0)
		return 0;
	s->ts_str = str;
	s->ts_n = tt_strp - s->ts_str - 1;
	return s;
}

struct tt_str *
ttxgetstr(const char *str)
{
	struct tt_str *s;
	char buf[100];
	char *bufp = buf;

	if (tgetstr(str, &bufp) == 0)
		return 0;
	if ((s = (struct tt_str *) malloc(sizeof *s)) == 0)
		return 0;
	s->ts_str = tt_strp;
	tputs(buf, 1, ttxputc);
	s->ts_n = tt_strp - s->ts_str;
	*tt_strp++ = 0;
	return s;
}

void
tttgoto(struct tt_str *s, int col, int row)
{
	const char *p = s->ts_str;

	ttputs(tgoto(p, col, row));
	for (p += s->ts_n; *--p == 0;)
		ttputc(0);
}

void
ttpgoto(struct tt_str *s, int col, int row, int n)
{

	tputs(tgoto(s->ts_str, col, row), n, tttputc);
}

int
ttstrcmp(struct tt_str *a, struct tt_str *b)
{
	int n, r;

	if ((r = memcmp(a->ts_str, b->ts_str,
			(n = a->ts_n - b->ts_n) < 0 ? a->ts_n : b->ts_n)))
		return r;
	return n;
}
