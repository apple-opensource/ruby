/**********************************************************************

  sprintf.c -

  $Author: jkh $
  $Date: 2002/05/27 17:59:44 $
  created at: Fri Oct 15 10:39:26 JST 1993

  Copyright (C) 1993-2000 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include <ctype.h>
#include <math.h>

#define BIT_DIGITS(N)   (((N)*146)/485 + 1)  /* log2(10) =~ 146/485 */

#if !defined(atof) && !defined(HAVE_STDLIB_H)
double strtod();
#endif

static void fmt_setup _((char*,int,int,int,int));

static char*
remove_sign_bits(str, base)
    char *str;
    int base;
{
    char *s, *t, *end;
    
    s = t = str;
    end = str + strlen(str);

    if (base == 16) {
      x_retry:
	switch (*t) {
	  case 'c': case 'C':
	    *t = '4';
	    break;
	  case 'd': case 'D':
	    *t = '5';
	    break;
	  case 'e': case 'E':
	    *t = '2';
	    break;
	  case 'f': case 'F':
	    if (t[1] > '8') {
		t++;
		goto x_retry;
	    }
	    *t = '1';
	    break;
	  case '1':
	  case '3':
	  case '7':
	    if (t[1] > '8') {
		t++;
		goto x_retry;
	    }
	    break;
	}
	switch (*t) {
	  case '1': *t = 'f'; break;
	  case '2': *t = 'e'; break;
	  case '3': *t = 'f'; break;
	  case '4': *t = 'c'; break;
	  case '5': *t = 'd'; break;
	  case '6': *t = 'e'; break;
	  case '7': *t = 'f'; break;
	}
    }
    else if (base == 8) {
      o_retry:
	switch (*t) {
	  case '6':
	    *t = '2';
	    break;
	  case '7':
	    if (t[1] > '3') {
		t++;
		goto o_retry;
	    }
	    *t = '1';
	    break;
	  case '1':
	  case '3':
	    if (t[1] > '3') {
		t++;
		goto o_retry;
	    }
	    break;
	}
	switch (*t) {
	  case '1': *t = '7'; break;
	  case '2': *t = '6'; break;
	  case '3': *t = '7'; break;
	}
    }
    else if (base == 2) {
	while (t<end && *t == '1') t++;
	t--;
    }
    while (*t) *s++ = *t++;
    *s = '\0';

    return str;
}

#define FNONE  0
#define FSHARP 1
#define FMINUS 2
#define FPLUS  4
#define FZERO  8
#define FSPACE 16
#define FWIDTH 32
#define FPREC  64

#define CHECK(l) {\
    while (blen + (l) >= bsiz) {\
	REALLOC_N(buf, char, bsiz*2);\
	bsiz*=2;\
    }\
}

#define PUSH(s, l) { \
    CHECK(l);\
    memcpy(&buf[blen], s, l);\
    blen += (l);\
}

#define GETARG() \
    ((nextarg >= argc) ? (rb_raise(rb_eArgError, "too few argument."), 0) : argv[nextarg++])

#define GETASTER(val) { \
    t = p++; \
    n = 0; \
    for (; p < end && ISDIGIT(*p); p++) { \
	n = 10 * n + (*p - '0'); \
    } \
    if (p >= end) { \
	rb_raise(rb_eArgError, "malformed format string - %%*[0-9]"); \
    } \
    if (*p == '$') { \
	int curarg = nextarg; \
	nextarg = n; \
	tmp = GETARG(); \
	nextarg = curarg; \
    } \
    else { \
	tmp = GETARG(); \
	p = t; \
    } \
    val = NUM2INT(tmp); \
}

VALUE
rb_f_sprintf(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE fmt;
    char *buf, *p, *end;
    int blen, bsiz;
    VALUE result;

    int width, prec, flags = FNONE;
    int nextarg = 0;
    int tainted = 0;
    VALUE tmp;
    VALUE str;

    fmt = GETARG();
    if (OBJ_TAINTED(fmt)) tainted = 1;
    p = rb_str2cstr(fmt, &blen);
    end = p + blen;
    blen = 0;
    bsiz = 120;
    buf = ALLOC_N(char, bsiz);

    for (; p < end; p++) {
	char *t;
	int n;

	for (t = p; t < end && *t != '%'; t++) ;
	CHECK(t - p);
	PUSH(p, t - p);
	if (t >= end) {
	    /* end of fmt string */
	    goto sprint_exit;
	}
	p = t + 1;		/* skip `%' */

	width = prec = -1;
      retry:
	switch (*p) {
	  default:
	    if (ISPRINT(*p))
		rb_raise(rb_eArgError, "malformed format string - %%%c", *p);
	    else
		rb_raise(rb_eArgError, "malformed format string");
	    break;

	  case ' ':
	    flags |= FSPACE;
	    p++;
	    goto retry;

	  case '#':
	    flags |= FSHARP;
	    p++;
	    goto retry;

	  case '+':
	    flags |= FPLUS;
	    p++;
	    goto retry;

	  case '-':
	    flags |= FMINUS;
	    p++;
	    goto retry;

	  case '0':
	    flags |= FZERO;
	    p++;
	    goto retry;

	  case '1': case '2': case '3': case '4':
	  case '5': case '6': case '7': case '8': case '9':
	    n = 0;
	    for (; p < end && ISDIGIT(*p); p++) {
		n = 10 * n + (*p - '0');
	    }
	    if (p >= end) {
		rb_raise(rb_eArgError, "malformed format string - %%[0-9]");
	    }
	    if (*p == '$') {
		nextarg = n;
		p++;
		goto retry;
	    }
	    width = n;
	    flags |= FWIDTH;
	    goto retry;

	  case '*':
	    if (flags & FWIDTH) {
		rb_raise(rb_eArgError, "width given twice");
	    }

	    flags |= FWIDTH;
	    GETASTER(width);
	    if (width < 0) {
		flags |= FMINUS;
		width = -width;
	    }
	    p++;
	    goto retry;

	  case '.':
	    if (flags & FPREC) {
		rb_raise(rb_eArgError, "precision given twice");
	    }
	    flags |= FPREC;

	    prec = 0;
	    p++;
	    if (*p == '*') {
		GETASTER(prec);
		if (prec < 0) {	/* ignore negative precision */
		    flags &= ~FPREC;
		}
		p++;
		goto retry;
	    }

	    for (; p < end && ISDIGIT(*p); p++) {
		prec = 10 * prec + (*p - '0');
	    }
	    if (p >= end) {
		rb_raise(rb_eArgError, "malformed format string - %%.[0-9]");
	    }
	    goto retry;

	  case '\n':
	    p--;
	  case '\0':
	  case '%':
	    if (flags != FNONE) {
		rb_raise(rb_eArgError, "illegal format character - %%");
	    }
	    PUSH("%", 1);
	    break;

	  case 'c':
	    {
		VALUE val = GETARG();
		char c;

		if (!(flags & FMINUS))
		    while (--width > 0)
			PUSH(" ", 1);
		c = NUM2INT(val) & 0xff;
		PUSH(&c, 1);
		while (--width > 0)
		    PUSH(" ", 1);
	    }
	    break;

	  case 's':
	    {
		VALUE arg = GETARG();
		int len;

		str = rb_obj_as_string(arg);
		if (OBJ_TAINTED(str)) tainted = 1;
		len = RSTRING(str)->len;
		if (flags&FPREC) {
		    if (prec < len) {
			len = prec;
		    }
		}
		if (flags&FWIDTH) {
		    if (width > len) {
			CHECK(width);
			width -= len;
			if (!(flags&FMINUS)) {
			    while (width--) {
				buf[blen++] = ' ';
			    }
			}
			memcpy(&buf[blen], RSTRING(str)->ptr, len);
			blen += len;
			if (flags&FMINUS) {
			    while (width--) {
				buf[blen++] = ' ';
			    }
			}
			break;
		    }
		}
		CHECK(len);
		memcpy(&buf[blen], RSTRING(str)->ptr, len);
		blen += len;
	    }
	    break;

	  case 'd':
	  case 'i':
	  case 'o':
	  case 'x':
	  case 'X':
	  case 'b':
	  case 'u':
	    {
		volatile VALUE val = GETARG();
		char fbuf[32], nbuf[64], *s, *t;
		char *prefix = 0;
		int sign = 0;
		char sc = 0;
		long v;
		int base, bignum = 0;
		int len, pos;

		switch (*p) {
		  case 'd':
		  case 'i':
		    sign = 1; break;
		  case 'o':
		  case 'x':
		  case 'X':
		  case 'b':
		  case 'u':
		  default:
		    if (flags&(FPLUS|FSPACE)) sign = 1;
		    break;
		}
		if (flags & FSHARP) {
		    if (*p == 'o') prefix = "0";
		    else if (*p == 'x') prefix = "0x";
		    else if (*p == 'X') prefix = "0X";
		    else if (*p == 'b') prefix = "0b";
		    if (prefix) {
			width -= strlen(prefix);
		    }
		}

	      bin_retry:
		switch (TYPE(val)) {
		  case T_FLOAT:
		    val = rb_dbl2big(RFLOAT(val)->value);
		    if (FIXNUM_P(val)) goto bin_retry;
		    bignum = 1;
		    break;
		  case T_STRING:
		    val = rb_str2inum(val, 0);
		    goto bin_retry;
		  case T_BIGNUM:
		    bignum = 1;
		    break;
		  default:
		    v = NUM2LONG(val);
		    break;
		  case T_FIXNUM:
		    v = FIX2LONG(val);
		    break;
		}

		if (*p == 'u' || *p == 'd' || *p == 'i') base = 10;
		else if (*p == 'x' || *p == 'X') base = 16;
		else if (*p == 'o') base = 8;
		else if (*p == 'b') base = 2;
		if (!bignum) {
		    if (base == 2) {
			val = rb_int2big(v);
			goto bin_retry;
		    }
		    if (sign) {
			char c = *p;
			if (c == 'i') c = 'd'; /* %d and %i are identical */
			if (v < 0) {
			    v = -v;
			    sc = '-';
			    width--;
			}
			else if (flags & FPLUS) {
			    sc = '+';
			    width--;
			}
			else if (flags & FSPACE) {
			    sc = ' ';
			    width--;
			}
			sprintf(fbuf, "%%l%c", c);
			sprintf(nbuf, fbuf, v);
			s = nbuf;
			goto format_integer;
		    }
		    s = nbuf;
		    if (v < 0) {
			strcpy(s, "..");
			s += 2;
		    }
		    sprintf(fbuf, "%%l%c", *p);
		    sprintf(s, fbuf, v);
		    if (v < 0) {
			char d = 0;

			remove_sign_bits(s, base);
			switch (base) {
			  case 16:
			    d = 'f'; 
			    break;
			  case 8:
			    d = '7'; break;
			}
			if (d && *s != d) {
			    memmove(s+1, s, strlen(s)+1);
			    *s = d;
			}
		    }
		    s = nbuf;
		    goto format_integer;
		}

		if (sign) {
		    val = rb_big2str(val, base);
		    s = RSTRING(val)->ptr;
		    if (s[0] == '-') {
			s++;
			sc = '-';
			width--;
		    }
		    else if (flags & FPLUS) {
			sc = '+';
			width--;
		    }
		    else if (flags & FSPACE) {
			sc = ' ';
			width--;
		    }
		    goto format_integer;
		}
		if (!RBIGNUM(val)->sign) {
		    val = rb_big_clone(val);
		    rb_big_2comp(val);
		}
		val = rb_big2str(val, base);
		s = RSTRING(val)->ptr;
		if (*s == '-') {
		    remove_sign_bits(++s, base);
		    val = rb_str_new(0, 3+strlen(s));
		    t = RSTRING(val)->ptr;
		    strcpy(t, "..");
		    t += 2;
		    switch (base) {
		      case 16:
			if (s[0] != 'f') strcpy(t++, "f"); break;
		      case 8:
			if (s[0] != '7') strcpy(t++, "7"); break;
		    }
		    strcpy(t, s);
		    bignum = 2;
		}
		s  = RSTRING(val)->ptr;

	      format_integer:
		pos = -1;
		len = strlen(s);

		if (*p == 'X') {
		    char *pp = s;
		    while (*pp) {
			*pp = toupper(*pp);
			pp++;
		    }
		}
		if (prec < len) prec = len;
		width -= prec;
		if (!(flags&(FZERO|FMINUS)) && s[0] != '.') {
		    CHECK(width);
		    while (width-->0) {
			buf[blen++] = ' ';
		    }
		}
		if (sc) PUSH(&sc, 1);
		if (prefix) {
		    int plen = strlen(prefix);
		    CHECK(plen);
		    strcpy(&buf[blen], prefix);
		    blen += plen;
		    if (pos) pos += plen;
		}
		if (!(flags & FMINUS)) {
		    char c = ' ';

		    if (s[0] == '.') {
			c = '.';
			if ((flags & FPREC) && prec > len) {
			    pos = blen;
			}
			else {
			    pos = blen + 2;
			}
		    }
		    else if (flags & FZERO) c = '0';
		    CHECK(width);
		    while (width-->0) {
			buf[blen++] = c;
		    }
		}
		CHECK(prec - len);
		while (len < prec--) {
		    buf[blen++] = s[0]=='.'?'.':'0';
		}
		CHECK(len);
		strcpy(&buf[blen], s);
		blen += len;
		CHECK(width);
		while (width-->0) {
		    buf[blen++] = ' ';
		}
		if (pos >= 0 && buf[pos] == '.') {
		    char c = '.';

		    switch (base) {
		      case 16:
			if (*p == 'X') c = 'F';
			else c = 'f';
			break;
		      case 8:
			c = '7'; break;
		      case 2:
			c = '1'; break;
		    }
		    s = &buf[pos];
		    while (*s && *s == '.') {
			*s++ = c;
		    }
		}
	    }
	    break;

	  case 'f':
	  case 'g':
	  case 'G':
	  case 'e':
	  case 'E':
	    {
		VALUE val = GETARG();
		double fval;
		int i, need = 6;
		char fbuf[32];

		switch (TYPE(val)) {
		  case T_FIXNUM:
		    fval = (double)FIX2LONG(val);
		    break;
		  case T_FLOAT:
		    fval = RFLOAT(val)->value;
		    break;
		  case T_BIGNUM:
		    fval = rb_big2dbl(val);
		    break;
		  case T_STRING:
		    fval = strtod(RSTRING(val)->ptr, 0);
		    break;
		  default:
		    fval = NUM2DBL(val);
		    break;
		}

		fmt_setup(fbuf, *p, flags, width, prec);
		need = 0;
		if (*p != 'e' && *p != 'E') {
		    i = INT_MIN;
		    frexp(fval, &i);
		    if (i > 0)
			need = BIT_DIGITS(i);
		}
		need += (flags&FPREC) ? prec : 6;
		if ((flags&FWIDTH) && need < width)
		    need = width;
		need += 20;

		CHECK(need);
		sprintf(&buf[blen], fbuf, fval);
		blen += strlen(&buf[blen]);
	    }
	    break;
	}
	flags = FNONE;
    }

  sprint_exit:
#if 0
    /* XXX - We cannot validiate the number of arguments because
     *       the format string may contain `n$'-style argument selector.
     */
    if (RTEST(ruby_verbose) && nextarg < argc) {
	rb_raise(rb_eArgError, "too many argument for format string");
    }
#endif
    result = rb_str_new(buf, blen);
    free(buf);

    if (tainted) OBJ_TAINT(result);
    return result;
}

static void
fmt_setup(buf, c, flags, width, prec)
    char *buf;
    int c;
    int flags, width, prec;
{
    *buf++ = '%';
    if (flags & FSHARP) *buf++ = '#';
    if (flags & FPLUS)  *buf++ = '+';
    if (flags & FMINUS) *buf++ = '-';
    if (flags & FZERO)  *buf++ = '0';
    if (flags & FSPACE) *buf++ = ' ';

    if (flags & FWIDTH) {
	sprintf(buf, "%d", width);
	buf += strlen(buf);
    }

    if (flags & FPREC) {
	sprintf(buf, ".%d", prec);
	buf += strlen(buf);
    }

    *buf++ = c;
    *buf = '\0';
}
