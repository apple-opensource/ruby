/**********************************************************************

  pack.c -

  $Author: melville $
  $Date: 2003/05/14 14:09:13 $
  created at: Thu Feb 10 15:17:05 JST 1994

  Copyright (C) 1993-2000 Yukihiro Matsumoto

**********************************************************************/

#include "ruby.h"
#include <sys/types.h>
#include <ctype.h>

#ifdef __BIG_ENDIAN__
#define WORDS_BIGENDIAN
#else
#undef WORDS_BIGENDIAN
#endif

#define SIZE16 2
#define SIZE32 4

#if SIZEOF_SHORT != 2 || SIZEOF_LONG != 4
# define NATINT_PACK
#endif

#ifdef NATINT_PACK
# define OFF16B(p) ((char*)(p) + (natint?0:(sizeof(short) - SIZE16)))
# define OFF32B(p) ((char*)(p) + (natint?0:(sizeof(long) - SIZE32)))
# define NATINT_I32(x) (natint?NUM2LONG(x):(NUM2I32(x)))
# define NATINT_U32(x) (natint?NUM2ULONG(x):(NUM2U32(x)))
# define NATINT_LEN(type,len) (natint?sizeof(type):(len))
# ifdef WORDS_BIGENDIAN
#   define OFF16(p) OFF16B(p)
#   define OFF32(p) OFF32B(p)
# endif
#else
# define NATINT_I32(x) NUM2I32(x)
# define NATINT_U32(x) NUM2U32(x)
# define NATINT_LEN(type,len) sizeof(type)
#endif

#ifndef OFF16
# define OFF16(p) (char*)(p)
# define OFF32(p) (char*)(p)
#endif

#ifndef OFF16B
# define OFF16B(p) (char*)(p)
# define OFF32B(p) (char*)(p)
#endif

#define define_swapx(x, xtype)		\
static xtype				\
TOKEN_PASTE(swap,x)(z)			\
    xtype z;				\
{					\
    xtype r;				\
    xtype *zp;				\
    unsigned char *s, *t;		\
    int i;				\
					\
    zp = (xtype *)malloc(sizeof(xtype));\
    *zp = z;				\
    s = (char *)zp;			\
    t = (char *)malloc(sizeof(xtype));	\
    for (i=0; i<sizeof(xtype); i++) {	\
	t[sizeof(xtype)-i-1] = s[i];	\
    }					\
    r = *(xtype *)t;			\
    free(t);				\
    free(zp);				\
    return r;				\
}

#if SIZEOF_SHORT == 2
#define swaps(x)	((((x)&0xFF)<<8) | (((x)>>8)&0xFF))
#else
#if SIZEOF_SHORT == 4
#define swaps(x)	((((x)&0xFF)<<24)	\
			|(((x)>>24)&0xFF)	\
			|(((x)&0x0000FF00)<<8)	\
			|(((x)&0x00FF0000)>>8)	)
#else
define_swapx(s,short);
#endif
#endif

#if SIZEOF_LONG == 4
#define swapl(x)	((((x)&0xFF)<<24)	\
			|(((x)>>24)&0xFF)	\
			|(((x)&0x0000FF00)<<8)	\
			|(((x)&0x00FF0000)>>8)	)
#else
#if SIZEOF_LONG == 8
#define swapl(x)        ((((x)&0x00000000000000FF)<<56)	\
			|(((x)&0xFF00000000000000)>>56)	\
			|(((x)&0x000000000000FF00)<<40)	\
			|(((x)&0x00FF000000000000)>>40)	\
			|(((x)&0x0000000000FF0000)<<24)	\
			|(((x)&0x0000FF0000000000)>>24)	\
			|(((x)&0x00000000FF000000)<<8)	\
			|(((x)&0x000000FF00000000)>>8))
#else
define_swapx(l,long);
#endif
#endif

#if SIZEOF_FLOAT == 4
#if SIZEOF_LONG == 4	/* SIZEOF_FLOAT == 4 == SIZEOF_LONG */
#define swapf(x)	swapl(x)
#define FLOAT_SWAPPER	unsigned long
#else
#if SIZEOF_SHORT == 4	/* SIZEOF_FLOAT == 4 == SIZEOF_SHORT */
#define swapf(x)	swaps(x)
#define FLOAT_SWAPPER	unsigned short
#else	/* SIZEOF_FLOAT == 4 but undivide by known size of int */
define_swapx(f,float);
#endif	/* #if SIZEOF_SHORT == 4 */
#endif	/* #if SIZEOF_LONG == 4 */
#else	/* SIZEOF_FLOAT != 4 */
define_swapx(f,float);
#endif	/* #if SIZEOF_FLOAT == 4 */

#if SIZEOF_DOUBLE == 8
#if SIZEOF_LONG == 8	/* SIZEOF_DOUBLE == 8 == SIZEOF_LONG */
#define swapd(x)	swapl(x)
#define DOUBLE_SWAPPER	unsigned long
#else
#if SIZEOF_LONG == 4	/* SIZEOF_DOUBLE == 8 && 4 == SIZEOF_LONG */
static double
swapd(d)
    const double d;
{
    double dtmp = d;
    unsigned long utmp[2];
    unsigned long utmp0;

    utmp[0] = 0; utmp[1] = 0;
    memcpy(utmp,&dtmp,sizeof(double));
    utmp0 = utmp[0];
    utmp[0] = swapl(utmp[1]);
    utmp[1] = swapl(utmp0);
    memcpy(&dtmp,utmp,sizeof(double));
    return dtmp;
}
#else
#if SIZEOF_SHORT == 4	/* SIZEOF_DOUBLE == 8 && 4 == SIZEOF_SHORT */
static double
swapd(d)
    const double d;
{
    double dtmp = d;
    unsigned short utmp[2];
    unsigned short utmp0;

    utmp[0] = 0; utmp[1] = 0;
    memcpy(utmp,&dtmp,sizeof(double));
    utmp0 = utmp[0];
    utmp[0] = swaps(utmp[1]);
    utmp[1] = swaps(utmp0);
    memcpy(&dtmp,utmp,sizeof(double));
    return dtmp;
}
#else	/* SIZEOF_DOUBLE == 8 but undivied by known size of int */
define_swapx(d, double);
#endif	/* #if SIZEOF_SHORT == 4 */
#endif	/* #if SIZEOF_LONG == 4 */
#endif	/* #if SIZEOF_LONG == 8 */
#else	/* SIZEOF_DOUBLE != 8 */
define_swapx(d, double);
#endif	/* #if SIZEOF_DPOUBLE == 8 */

#undef define_swapx

#ifdef DYNAMIC_ENDIAN
#ifdef ntohs
#undef ntohs
#undef ntohl
#undef htons
#undef htonl
#endif
static int
endian()
{
    static int init = 0;
    static int endian_value;
    char *p;

    if (init) return endian_value;
    init = 1;
    p = (char*)&init;
    return endian_value = p[0]?0:1;
}

#define ntohs(x) (endian()?(x):swaps(x))
#define ntohl(x) (endian()?(x):swapl(x))
#define ntohf(x) (endian()?(x):swapf(x))
#define ntohd(x) (endian()?(x):swapd(x))
#define htons(x) (endian()?(x):swaps(x))
#define htonl(x) (endian()?(x):swapl(x))
#define htonf(x) (endian()?(x):swapf(x))
#define htond(x) (endian()?(x):swapd(x))
#define htovs(x) (endian()?swaps(x):(x))
#define htovl(x) (endian()?swapl(x):(x))
#define htovf(x) (endian()?swapf(x):(x))
#define htovd(x) (endian()?swapd(x):(x))
#define vtohs(x) (endian()?swaps(x):(x))
#define vtohl(x) (endian()?swapl(x):(x))
#define vtohf(x) (endian()?swapf(x):(x))
#define vtohd(x) (endian()?swapd(x):(x))
#else
#ifdef WORDS_BIGENDIAN
#ifndef ntohs
#define ntohs(x) (x)
#define ntohl(x) (x)
#define htons(x) (x)
#define htonl(x) (x)
#endif
#define ntohf(x) (x)
#define ntohd(x) (x)
#define htonf(x) (x)
#define htond(x) (x)
#define htovs(x) swaps(x)
#define htovl(x) swapl(x)
#define htovf(x) swapf(x)
#define htovd(x) swapd(x)
#define vtohs(x) swaps(x)
#define vtohl(x) swapl(x)
#define vtohf(x) swapf(x)
#define vtohd(x) swapd(x)
#else /* LITTLE ENDIAN */
#ifndef ntohs
#undef ntohs
#undef ntohl
#undef htons
#undef htonl
#define ntohs(x) swaps(x)
#define ntohl(x) swapl(x)
#define htons(x) swaps(x)
#define htonl(x) swapl(x)
#endif
#define ntohf(x) swapf(x)
#define ntohd(x) swapd(x)
#define htonf(x) swapf(x)
#define htond(x) swapd(x)
#define htovs(x) (x)
#define htovl(x) (x)
#define htovf(x) (x)
#define htovd(x) (x)
#define vtohs(x) (x)
#define vtohl(x) (x)
#define vtohf(x) (x)
#define vtohd(x) (x)
#endif
#endif

#ifdef FLOAT_SWAPPER
#define FLOAT_CONVWITH(y)	FLOAT_SWAPPER y;
#define HTONF(x,y)	(memcpy(&y,&x,sizeof(float)),	\
			 y = htonf((FLOAT_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(float)),	\
			 x)
#define HTOVF(x,y)	(memcpy(&y,&x,sizeof(float)),	\
			 y = htovf((FLOAT_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(float)),	\
			 x)
#define NTOHF(x,y)	(memcpy(&y,&x,sizeof(float)),	\
			 y = ntohf((FLOAT_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(float)),	\
			 x)
#define VTOHF(x,y)	(memcpy(&y,&x,sizeof(float)),	\
			 y = vtohf((FLOAT_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(float)),	\
			 x)
#else
#define FLOAT_CONVWITH(y)
#define HTONF(x,y)	htonf(x)
#define HTOVF(x,y)	htovf(x)
#define NTOHF(x,y)	ntohf(x)
#define VTOHF(x,y)	vtohf(x)
#endif

#ifdef DOUBLE_SWAPPER
#define DOUBLE_CONVWITH(y)	DOUBLE_SWAPPER y;
#define HTOND(x,y)	(memcpy(&y,&x,sizeof(double)),	\
			 y = htond((DOUBLE_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(double)),	\
			 x)
#define HTOVD(x,y)	(memcpy(&y,&x,sizeof(double)),	\
			 y = htovd((DOUBLE_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(double)),	\
			 x)
#define NTOHD(x,y)	(memcpy(&y,&x,sizeof(double)),	\
			 y = ntohd((DOUBLE_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(double)),	\
			 x)
#define VTOHD(x,y)	(memcpy(&y,&x,sizeof(double)),	\
			 y = vtohd((DOUBLE_SWAPPER)y),	\
			 memcpy(&x,&y,sizeof(double)),	\
			 x)
#else
#define DOUBLE_CONVWITH(y)
#define HTOND(x,y)	htond(x)
#define HTOVD(x,y)	htovd(x)
#define NTOHD(x,y)	ntohd(x)
#define VTOHD(x,y)	vtohd(x)
#endif

#if SIZEOF_LONG == SIZE32
typedef long I32;
typedef unsigned long U32;
#define NUM2I32(x) NUM2LONG(x)
#define NUM2U32(x) NUM2ULONG(x)
#elif SIZEOF_INT == SIZE32
typedef int I32;
typedef unsigned int U32;
#define NUM2I32(x) NUM2INT(x)
#define NUM2U32(x) NUM2UINT(x)
#endif

static char *toofew = "too few arguments";

static void encodes _((VALUE,char*,int,int));
static void qpencode _((VALUE,VALUE,int));

static int uv_to_utf8 _((char*,unsigned long));
static unsigned long utf8_to_uv _((char*,int*));

static VALUE
pack_pack(ary, fmt)
    VALUE ary, fmt;
{
    static char *nul10 = "\0\0\0\0\0\0\0\0\0\0";
    static char *spc10 = "          ";
    char *p, *pend;
    VALUE res, from;
    char type;
    int items, len, idx;
    char *ptr;
    int plen;
#ifdef NATINT_PACK
    int natint;		/* native integer */
#endif
    
    p = rb_str2cstr(fmt, &plen);
    pend = p + plen;
    res = rb_str_new(0, 0);

    items = RARRAY(ary)->len;
    idx = 0;

#define NEXTFROM (items-- > 0 ? RARRAY(ary)->ptr[idx++] : (rb_raise(rb_eArgError, toofew),0))

    while (p < pend) {
	type = *p++;		/* get data type */
#ifdef NATINT_PACK
	natint = 0;
#endif

	if (ISSPACE(type)) continue;
        if (*p == '_' || *p == '!') {
	    char *natstr = "sSiIlL";

	    if (strchr(natstr, type)) {
#ifdef NATINT_PACK
		natint = 1;
#endif
		p++;
	    }
	    else {
		rb_raise(rb_eArgError, "'%c' allowed only after types %s", *p, natstr);
	    }
	}
	if (*p == '*') {	/* set data length */
	    len = strchr("@Xxu", type) ? 0 : items;
            p++;
	}
	else if (ISDIGIT(*p)) {
	    len = strtoul(p, (char**)&p, 10);
	}
	else {
	    len = 1;
	}

	switch (type) {
	  case 'A': case 'a': case 'Z':
	  case 'B': case 'b':
	  case 'H': case 'h':
	    from = NEXTFROM;
	    if (NIL_P(from)) {
		ptr = "";
		plen = 0;
	    }
	    else {
		ptr = rb_str2cstr(from, &plen);
	    }

	    if (p[-1] == '*')
		len = plen;

	    switch (type) {
	      case 'a':
	      case 'A':
	      case 'Z':
		if (plen >= len)
		    rb_str_cat(res, ptr, len);
		else {
		    rb_str_cat(res, ptr, plen);
		    len -= plen;
		    while (len >= 10) {
			rb_str_cat(res, (type == 'A')?spc10:nul10, 10);
			len -= 10;
		    }
		    rb_str_cat(res, (type == 'A')?spc10:nul10, len);
		}
		break;

	      case 'b':
		{
		    int byte = 0;
		    int i, j = 0;

		    if (len > plen) {
			j = (len - plen + 1)/2;
			len = plen;
		    }
		    for (i=0; i++ < len; ptr++) {
			if (*ptr & 1)
			    byte |= 128;
			if (i & 7)
			    byte >>= 1;
			else {
			    char c = byte & 0xff;
			    rb_str_cat(res, &c, 1);
			    byte = 0;
			}
		    }
		    if (len & 7) {
			char c;
			byte >>= 7 - (len & 7);
			c = byte & 0xff;
			rb_str_cat(res, &c, 1);
		    }
		    len = RSTRING(res)->len;
		    rb_str_resize(res, len+j);
		    MEMZERO(RSTRING(res)->ptr+len, char, j);
		}
		break;

	      case 'B':
		{
		    int byte = 0;
		    int i, j = 0;

		    if (len > plen) {
			j = (len - plen + 1)/2;
			len = plen;
		    }
		    for (i=0; i++ < len; ptr++) {
			byte |= *ptr & 1;
			if (i & 7)
			    byte <<= 1;
			else {
			    char c = byte & 0xff;
			    rb_str_cat(res, &c, 1);
			    byte = 0;
			}
		    }
		    if (len & 7) {
			char c;
			byte <<= 7 - (len & 7);
			c = byte & 0xff;
			rb_str_cat(res, &c, 1);
		    }
		    len = RSTRING(res)->len;
		    rb_str_resize(res, len+j);
		    MEMZERO(RSTRING(res)->ptr+len, char, j);
		}
		break;

	      case 'h':
		{
		    int byte = 0;
		    int i, j = 0;

		    if (len > plen) {
			j = (len - plen + 1)/2;
			len = plen;
		    }
		    for (i=0; i++ < len; ptr++) {
			if (ISALPHA(*ptr))
			    byte |= (((*ptr & 15) + 9) & 15) << 4;
			else
			    byte |= (*ptr & 15) << 4;
			if (i & 1)
			    byte >>= 4;
			else {
			    char c = byte & 0xff;
			    rb_str_cat(res, &c, 1);
			    byte = 0;
			}
		    }
		    if (len & 1) {
			char c = byte & 0xff;
			rb_str_cat(res, &c, 1);
		    }
		    len = RSTRING(res)->len;
		    rb_str_resize(res, len+j);
		    MEMZERO(RSTRING(res)->ptr+len, char, j);
		}
		break;

	      case 'H':
		{
		    int byte = 0;
		    int i, j = 0;

		    if (len > plen) {
			j = (len - plen + 1)/2;
			len = plen;
		    }
		    for (i=0; i++ < len; ptr++) {
			if (ISALPHA(*ptr))
			    byte |= ((*ptr & 15) + 9) & 15;
			else
			    byte |= *ptr & 15;
			if (i & 1)
			    byte <<= 4;
			else {
			    char c = byte & 0xff;
			    rb_str_cat(res, &c, 1);
			    byte = 0;
			}
		    }
		    if (len & 1) {
			char c = byte & 0xff;
			rb_str_cat(res, &c, 1);
		    }
		    len = RSTRING(res)->len;
		    rb_str_resize(res, len+j);
		    MEMZERO(RSTRING(res)->ptr+len, char, j);
		}
		break;
	    }
	    break;

	  case 'c':
	  case 'C':
	    while (len-- > 0) {
		char c;

		from = NEXTFROM;
		if (NIL_P(from)) c = 0;
		else {
		    c = NUM2INT(from);
		}
		rb_str_cat(res, &c, sizeof(char));
	    }
	    break;

	  case 's':
	  case 'S':
	    while (len-- > 0) {
		short s;

		from = NEXTFROM;
		if (NIL_P(from)) s = 0;
		else {
		    s = NUM2INT(from);
		}
		rb_str_cat(res, OFF16(&s), NATINT_LEN(short,2));
	    }
	    break;

	  case 'i':
	  case 'I':
	    while (len-- > 0) {
		int i;

		from = NEXTFROM;
		if (NIL_P(from)) i = 0;
		else {
		    i = NUM2UINT(from);
		}
		rb_str_cat(res, (char*)&i, sizeof(int));
	    }
	    break;

	  case 'l':
	  case 'L':
	    while (len-- > 0) {
		long l;

		from = NEXTFROM;
		if (NIL_P(from)) l = 0;
		else {
		    l = NATINT_U32(from);
		}
		rb_str_cat(res, OFF32(&l), NATINT_LEN(long,4));
	    }
	    break;

	  case 'n':
	    while (len-- > 0) {
		unsigned short s;

		from = NEXTFROM;
		if (NIL_P(from)) s = 0;
		else {
		    s = NUM2INT(from);
		}
		s = htons(s);
		rb_str_cat(res, OFF16B(&s), NATINT_LEN(short,2));
	    }
	    break;

	  case 'N':
	    while (len-- > 0) {
		unsigned long l;

		from = NEXTFROM;
		if (NIL_P(from)) l = 0;
		else {
		    l = NATINT_U32(from);
		}
		l = htonl(l);
		rb_str_cat(res, OFF32B(&l), NATINT_LEN(long,4));
	    }
	    break;

	  case 'v':
	    while (len-- > 0) {
		unsigned short s;

		from = NEXTFROM;
		if (NIL_P(from)) s = 0;
		else {
		    s = NUM2INT(from);
		}
		s = htovs(s);
		rb_str_cat(res, OFF16(&s), NATINT_LEN(short,2));
	    }
	    break;

	  case 'V':
	    while (len-- > 0) {
		unsigned long l;

		from = NEXTFROM;
		if (NIL_P(from)) l = 0;
		else {
		    l = NATINT_U32(from);
		}
		l = htovl(l);
		rb_str_cat(res, OFF32(&l), NATINT_LEN(long,4));
	    }
	    break;

	  case 'f':
	  case 'F':
	    while (len-- > 0) {
		float f;

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    f = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    f = strtod(RSTRING(from)->ptr, 0);
		  default:
		    f = (float)NUM2INT(from);
		    break;
		}
		rb_str_cat(res, (char*)&f, sizeof(float));
	    }
	    break;

	  case 'e':
	    while (len-- > 0) {
		float f;
		FLOAT_CONVWITH(ftmp);

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    f = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    f = strtod(RSTRING(from)->ptr, 0);
		  default:
		    f = (float)NUM2INT(from);
		    break;
		}
		f = HTOVF(f,ftmp);
		rb_str_cat(res, (char*)&f, sizeof(float));
	    }
	    break;

	  case 'E':
	    while (len-- > 0) {
		double d;
		DOUBLE_CONVWITH(dtmp);

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    d = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    d = strtod(RSTRING(from)->ptr, 0);
		  default:
		    d = (double)NUM2INT(from);
		    break;
		}
		d = HTOVD(d,dtmp);
		rb_str_cat(res, (char*)&d, sizeof(double));
	    }
	    break;

	  case 'd':
	  case 'D':
	    while (len-- > 0) {
		double d;

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    d = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    d = strtod(RSTRING(from)->ptr, 0);
		  default:
		    d = (double)NUM2INT(from);
		    break;
		}
		rb_str_cat(res, (char*)&d, sizeof(double));
	    }
	    break;

	  case 'g':
	    while (len-- > 0) {
		float f;
		FLOAT_CONVWITH(ftmp);

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    f = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    f = strtod(RSTRING(from)->ptr, 0);
		  default:
		    f = (float)NUM2INT(from);
		    break;
		}
		f = HTONF(f,ftmp);
		rb_str_cat(res, (char*)&f, sizeof(float));
	    }
	    break;

	  case 'G':
	    while (len-- > 0) {
		double d;
		DOUBLE_CONVWITH(dtmp);

		from = NEXTFROM;
		switch (TYPE(from)) {
		  case T_FLOAT:
		    d = RFLOAT(from)->value;
		    break;
		  case T_STRING:
		    d = strtod(RSTRING(from)->ptr, 0);
		  default:
		    d = (double)NUM2INT(from);
		    break;
		}
		d = HTOND(d,dtmp);
		rb_str_cat(res, (char*)&d, sizeof(double));
	    }
	    break;

	  case 'x':
	  grow:
	    while (len >= 10) {
		rb_str_cat(res, nul10, 10);
		len -= 10;
	    }
	    rb_str_cat(res, nul10, len);
	    break;

	  case 'X':
	  shrink:
	    if (RSTRING(res)->len < len)
		rb_raise(rb_eArgError, "X outside of string");
	    RSTRING(res)->len -= len;
	    RSTRING(res)->ptr[RSTRING(res)->len] = '\0';
	    break;

	  case '@':
	    len -= RSTRING(res)->len;
	    if (len > 0) goto grow;
	    len = -len;
	    if (len > 0) goto shrink;
	    break;

	  case '%':
	    rb_raise(rb_eArgError, "%% is not supported");
	    break;

	  case 'U':
	    while (len-- > 0) {
		unsigned long l;
		char buf[8];
		int le;

		from = NEXTFROM;
		if (NIL_P(from)) l = 0;
		else {
		    l = NUM2ULONG(from);
		}
		le = uv_to_utf8(buf, l);
		rb_str_cat(res, (char*)buf, le);
	    }
	    break;

	  case 'u':
	  case 'm':
	    ptr = rb_str2cstr(NEXTFROM, &plen);

	    if (len <= 2)
		len = 45;
	    else
		len = len / 3 * 3;
	    while (plen > 0) {
		int todo;

		if (plen > len)
		    todo = len;
		else
		    todo = plen;
		encodes(res, ptr, todo, type);
		plen -= todo;
		ptr += todo;
	    }
	    break;

	  case 'M':
	    from = rb_obj_as_string(NEXTFROM);
	    if (len <= 1)
		len = 72;
	    qpencode(res, from, len);
	    break;

	  case 'P':
	    len = 1;
	    /* FALL THROUGH */
	  case 'p':
	    while (len-- > 0) {
		char *t;
		from = NEXTFROM;
		if (NIL_P(from)) t = "";
		else {
		    t = STR2CSTR(from);
		    rb_str_associate(res, from);
		}
		rb_str_cat(res, (char*)&t, sizeof(char*));
	    }
	    break;

	  case 'w':
	    while (len-- > 0) {
		unsigned long ul;
		VALUE buf = rb_str_new(0, 0);
		char c, *bufs, *bufe;

		from = NEXTFROM;

		if (TYPE(from) == T_BIGNUM) {
		    VALUE big128 = rb_uint2big(128);
		    while (TYPE(from) == T_BIGNUM) {
			from = rb_big_divmod(from, big128);
			c = NUM2INT(RARRAY(from)->ptr[1]) | 0x80; /* mod */
			rb_str_cat(buf, &c, sizeof(char));
			from = RARRAY(from)->ptr[0]; /* div */
		    }
		}

		if (NIL_P(from)) ul = 0;
		else {
		    ul = NUM2ULONG(from);
		}

		while (ul) {
		    c = ((ul & 0x7f) | 0x80);
		    rb_str_cat(buf, &c, sizeof(char));
		    ul >>=  7;
		}

		if (RSTRING(buf)->len) {
		    bufs = RSTRING(buf)->ptr;
		    bufe = bufs + RSTRING(buf)->len - 1;
		    *bufs &= 0x7f; /* clear continue bit */
		    while (bufs < bufe) { /* reverse */
			c = *bufs;
			*bufs++ = *bufe;
			*bufe-- = c;
		    }
		    rb_str_cat(res, RSTRING(buf)->ptr, RSTRING(buf)->len);
		}
		else {
		    c = 0;
		    rb_str_cat(res, &c, sizeof(char));
		}
	    }
	    break;

	  default:
	    break;
	}
    }

    return res;
}

static char uu_table[] =
"`!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";
static char b64_table[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
encodes(str, s, len, type)
    VALUE str;
    char *s;
    int len;
    int type;
{
    char *buff = ALLOCA_N(char, len * 4 / 3 + 6);
    int i = 0;
    char *trans = type == 'u' ? uu_table : b64_table;
    int padding;

    if (type == 'u') {
	buff[i++] = len + ' ';
	padding = '`';
    }
    else {
	padding = '=';
    }
    while (len >= 3) {
	buff[i++] = trans[077 & (*s >> 2)];
	buff[i++] = trans[077 & (((*s << 4) & 060) | ((s[1] >> 4) & 017))];
	buff[i++] = trans[077 & (((s[1] << 2) & 074) | ((s[2] >> 6) & 03))];
	buff[i++] = trans[077 & s[2]];
	s += 3;
	len -= 3;
    }
    if (len == 2) {
	buff[i++] = trans[077 & (*s >> 2)];
	buff[i++] = trans[077 & (((*s << 4) & 060) | ((s[1] >> 4) & 017))];
	buff[i++] = trans[077 & (((s[1] << 2) & 074) | (('\0' >> 6) & 03))];
	buff[i++] = padding;
    }
    else if (len == 1) {
	buff[i++] = trans[077 & (*s >> 2)];
	buff[i++] = trans[077 & (((*s << 4) & 060) | (('\0' >> 4) & 017))];
	buff[i++] = padding;
	buff[i++] = padding;
    }
    buff[i++] = '\n';
    rb_str_cat(str, buff, i);
}

static char hex_table[] = "0123456789ABCDEF";

static void
qpencode(str, from, len)
    VALUE str, from;
    int len;
{
    char buff[1024];
    int i = 0, n = 0, prev = EOF;
    unsigned char *s = (unsigned char*)RSTRING(from)->ptr;
    unsigned char *send = s + RSTRING(from)->len;

    while (s < send) {
        if ((*s > 126) ||
	    (*s < 32 && *s != '\n' && *s != '\t') ||
	    (*s == '=')) {
	    buff[i++] = '=';
	    buff[i++] = hex_table[*s >> 4];
	    buff[i++] = hex_table[*s & 0x0f];
            n += 3;
            prev = EOF;
        }
	else if (*s == '\n') {
            if (prev == ' ' || prev == '\t') {
		buff[i++] = '=';
		buff[i++] = *s;
            }
	    buff[i++] = *s;
            n = 0;
            prev = *s;
        }
	else {
	    buff[i++] = *s;
            n++;
            prev = *s;
        }
        if (n > len) {
	    buff[i++] = '=';
	    buff[i++] = '\n';
            n = 0;
            prev = '\n';
        }
	if (i > 1024 - 5) {
	    rb_str_cat(str, buff, i);
	    i = 0;
	}
	s++;
    }
    if (n > 0) {
	buff[i++] = '=';
	buff[i++] = '\n';
    }
    if (i > 0) {
	rb_str_cat(str, buff, i);
    }
}

static inline int
hex2num(c)
    char c;
{
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return c - '0';
    case 'a': case 'b': case 'c':
    case 'd': case 'e': case 'f':
	return c - 'a' + 10;
    case 'A': case 'B': case 'C':
    case 'D': case 'E': case 'F':
	return c - 'A' + 10;
    default:
	return -1;
    }
}

#ifdef NATINT_PACK
#define PACK_LENGTH_ADJUST(type,sz) do {	\
    int t__len = NATINT_LEN(type,(sz));		\
    tmp = 0;					\
    if (len > (send-s)/t__len) {		\
        if (!star) {				\
	    tmp = len-(send-s)/t__len;		\
        }					\
	len = (send-s)/t__len;			\
    }						\
} while (0)
#else
#define PACK_LENGTH_ADJUST(type,sz) do {	\
    tmp = 0;					\
    if (len > (send-s)/sizeof(type)) {		\
        if (!star) {				\
	    tmp = len - (send-s)/sizeof(type);	\
        }					\
	len = (send-s)/sizeof(type);		\
    }						\
} while (0)
#endif

#define PACK_ITEM_ADJUST() while (tmp--) rb_ary_push(ary, Qnil);

static VALUE
pack_unpack(str, fmt)
    VALUE str, fmt;
{
    static char *hexdigits = "0123456789abcdef0123456789ABCDEFx";
    char *s, *send;
    char *p, *pend;
    VALUE ary;
    char type;
    int len, tmp, star;
#ifdef NATINT_PACK
    int natint;			/* native integer */
#endif

    s = rb_str2cstr(str, &len);
    send = s + len;
    p = rb_str2cstr(fmt, &len);
    pend = p + len;

    ary = rb_ary_new();
    while (p < pend) {
#ifdef NATINT_PACK
	natint = 0;
#endif
	star = 0;
	type = *p++;
	if (*p == '_' || *p == '!') {
	    char *natstr = "sSiIlL";

	    if (strchr(natstr, type)) {
#ifdef NATINT_PACK
		natint = 1;
#endif
		p++;
	    }
	    else {
		rb_raise(rb_eArgError, "'%c' allowed only after types %s", *p, natstr);
	    }
	}
	if (p >= pend)
	    len = 1;
	else if (*p == '*') {
	    star = 1;
	    len = send - s;
	    p++;
	}
	else if (ISDIGIT(*p)) {
	    len = strtoul(p, (char**)&p, 10);
	}
	else {
	    len = (type != '@');
	}

	switch (type) {
	  case '%':
	    rb_raise(rb_eArgError, "%% is not supported");
	    break;

	  case 'A':
	    if (len > send - s) len = send - s;
	    {
		int end = len;
		char *t = s + len - 1;

		while (t >= s) {
		    if (*t != ' ' && *t != '\0') break;
		    t--; len--;
		}
		rb_ary_push(ary, rb_str_new(s, len));
		s += end;
	    }
	    break;

	  case 'Z':
	    if (len > send - s) len = send - s;
	    {
		int end = len;
		char *t = s + len - 1;

		while (t >= s) {
		    if (*t) break;
		    t--; len--;
		}
		rb_ary_push(ary, rb_str_new(s, len));
		s += end;
	    }
	    break;

	  case 'a':
	    if (len > send - s) len = send - s;
	    rb_ary_push(ary, rb_str_new(s, len));
	    s += len;
	    break;


	  case 'b':
	    {
		VALUE bitstr;
		char *t;
		int bits, i;

		if (p[-1] == '*' || len > (send - s) * 8)
		    len = (send - s) * 8;
		bits = 0;
		rb_ary_push(ary, bitstr = rb_str_new(0, len));
		t = RSTRING(bitstr)->ptr;
		for (i=0; i<len; i++) {
		    if (i & 7) bits >>= 1;
		    else bits = *s++;
		    *t++ = (bits & 1) ? '1' : '0';
		}
	    }
	    break;

	  case 'B':
	    {
		VALUE bitstr;
		char *t;
		int bits, i;

		if (p[-1] == '*' || len > (send - s) * 8)
		    len = (send - s) * 8;
		bits = 0;
		rb_ary_push(ary, bitstr = rb_str_new(0, len));
		t = RSTRING(bitstr)->ptr;
		for (i=0; i<len; i++) {
		    if (i & 7) bits <<= 1;
		    else bits = *s++;
		    *t++ = (bits & 128) ? '1' : '0';
		}
	    }
	    break;

	  case 'h':
	    {
		VALUE bitstr;
		char *t;
		int bits, i;

		if (p[-1] == '*' || len > (send - s) * 2)
		    len = (send - s) * 2;
		bits = 0;
		rb_ary_push(ary, bitstr = rb_str_new(0, len));
		t = RSTRING(bitstr)->ptr;
		for (i=0; i<len; i++) {
		    if (i & 1)
			bits >>= 4;
		    else
			bits = *s++;
		    *t++ = hexdigits[bits & 15];
		}
	    }
	    break;

	  case 'H':
	    {
		VALUE bitstr;
		char *t;
		int bits, i;

		if (p[-1] == '*' || len > (send - s) * 2)
		    len = (send - s) * 2;
		bits = 0;
		rb_ary_push(ary, bitstr = rb_str_new(0, len));
		t = RSTRING(bitstr)->ptr;
		for (i=0; i<len; i++) {
		    if (i & 1)
			bits <<= 4;
		    else
			bits = *s++;
		    *t++ = hexdigits[(bits >> 4) & 15];
		}
	    }
	    break;

	  case 'c':
	    PACK_LENGTH_ADJUST(char,sizeof(char));
	    while (len-- > 0) {
                int c = *s++;
                if (c > (char)127) c-=256;
		rb_ary_push(ary, INT2FIX(c));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'C':
	    PACK_LENGTH_ADJUST(unsigned char,sizeof(unsigned char));
	    while (len-- > 0) {
		unsigned char c = *s++;
		rb_ary_push(ary, INT2FIX(c));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 's':
	    PACK_LENGTH_ADJUST(short,2);
	    while (len-- > 0) {
		short tmp = 0;
		memcpy(OFF16(&tmp), s, NATINT_LEN(short,2));
		s += NATINT_LEN(short,2);
		rb_ary_push(ary, INT2FIX(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'S':
	    PACK_LENGTH_ADJUST(unsigned short,2);
	    while (len-- > 0) {
		unsigned short tmp = 0;
		memcpy(OFF16(&tmp), s, NATINT_LEN(unsigned short,2));
		s += NATINT_LEN(unsigned short,2);
		rb_ary_push(ary, INT2FIX(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'i':
	    PACK_LENGTH_ADJUST(int,sizeof(int));
	    while (len-- > 0) {
		int tmp;
		memcpy(&tmp, s, sizeof(int));
		s += sizeof(int);
		rb_ary_push(ary, rb_int2inum(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'I':
	    PACK_LENGTH_ADJUST(unsigned int,sizeof(unsigned int));
	    while (len-- > 0) {
		unsigned int tmp;
		memcpy(&tmp, s, sizeof(unsigned int));
		s += sizeof(unsigned int);
		rb_ary_push(ary, rb_uint2inum(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'l':
	    PACK_LENGTH_ADJUST(long,4);
	    while (len-- > 0) {
		long tmp = 0;
		memcpy(OFF32(&tmp), s, NATINT_LEN(long,4));
		s += NATINT_LEN(long,4);
		rb_ary_push(ary, rb_int2inum(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'L':
	    PACK_LENGTH_ADJUST(unsigned long,4);
	    while (len-- > 0) {
		unsigned long tmp = 0;
		memcpy(OFF32(&tmp), s, NATINT_LEN(unsigned long,4));
		s += NATINT_LEN(unsigned long,4);
		rb_ary_push(ary, rb_uint2inum(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'n':
	    PACK_LENGTH_ADJUST(unsigned short,2);
	    while (len-- > 0) {
		unsigned short tmp = 0;
		memcpy(OFF16B(&tmp), s, NATINT_LEN(unsigned short,2));
		s += NATINT_LEN(unsigned short,2);
		rb_ary_push(ary, rb_uint2inum(ntohs(tmp)));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'N':
	    PACK_LENGTH_ADJUST(unsigned long,4);
	    while (len-- > 0) {
		unsigned long tmp = 0;
		memcpy(OFF32B(&tmp), s, NATINT_LEN(unsigned long,4));
		s += NATINT_LEN(unsigned long,4);
		rb_ary_push(ary, rb_uint2inum(ntohl(tmp)));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'v':
	    PACK_LENGTH_ADJUST(unsigned short,2);
	    while (len-- > 0) {
		unsigned short tmp = 0;
		memcpy(OFF16(&tmp), s, NATINT_LEN(unsigned short,2));
		s += NATINT_LEN(unsigned short,2);
		rb_ary_push(ary, rb_uint2inum(vtohs(tmp)));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'V':
	    PACK_LENGTH_ADJUST(unsigned long,4);
	    while (len-- > 0) {
		unsigned long tmp = 0;
		memcpy(OFF32(&tmp), s, NATINT_LEN(long,4));
		s += NATINT_LEN(long,4);
		rb_ary_push(ary, rb_uint2inum(vtohl(tmp)));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'f':
	  case 'F':
	    PACK_LENGTH_ADJUST(float,sizeof(float));
	    while (len-- > 0) {
		float tmp;
		memcpy(&tmp, s, sizeof(float));
		s += sizeof(float);
		rb_ary_push(ary, rb_float_new((double)tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'e':
	    PACK_LENGTH_ADJUST(float,sizeof(float));
	    while (len-- > 0) {
	        float tmp;
		FLOAT_CONVWITH(ftmp);

		memcpy(&tmp, s, sizeof(float));
		s += sizeof(float);
		tmp = VTOHF(tmp,ftmp);
		rb_ary_push(ary, rb_float_new((double)tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;
	    
	  case 'E':
	    PACK_LENGTH_ADJUST(double,sizeof(double));
	    while (len-- > 0) {
		double tmp;
		DOUBLE_CONVWITH(dtmp);

		memcpy(&tmp, s, sizeof(double));
		s += sizeof(double);
		tmp = VTOHD(tmp,dtmp);
		rb_ary_push(ary, rb_float_new(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;
	    
	  case 'D':
	  case 'd':
	    PACK_LENGTH_ADJUST(double,sizeof(double));
	    while (len-- > 0) {
		double tmp;
		memcpy(&tmp, s, sizeof(double));
		s += sizeof(double);
		rb_ary_push(ary, rb_float_new(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;

	  case 'g':
	    PACK_LENGTH_ADJUST(float,sizeof(float));
	    while (len-- > 0) {
	        float tmp;
		FLOAT_CONVWITH(ftmp;)

		memcpy(&tmp, s, sizeof(float));
		s += sizeof(float);
		tmp = NTOHF(tmp,ftmp);
		rb_ary_push(ary, rb_float_new((double)tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;
	    
	  case 'G':
	    PACK_LENGTH_ADJUST(double,sizeof(double));
	    while (len-- > 0) {
		double tmp;
		DOUBLE_CONVWITH(dtmp);

		memcpy(&tmp, s, sizeof(double));
		s += sizeof(double);
		tmp = NTOHD(tmp,dtmp);
		rb_ary_push(ary, rb_float_new(tmp));
	    }
	    PACK_ITEM_ADJUST();
	    break;
	    
	  case 'U':
	    if (len > send - s) len = send - s;
	    while (len > 0 && s < send) {
		int alen = send - s;
		unsigned long l;

		l = utf8_to_uv(s, &alen);
		s += alen; len--;
		rb_ary_push(ary, rb_uint2inum(l));
	    }
	    break;

	  case 'u':
	    {
		VALUE str = rb_str_new(0, (send - s)*3/4);
		char *ptr = RSTRING(str)->ptr;
		int total = 0;

		while (s < send && *s > ' ' && *s < 'a') {
		    long a,b,c,d;
		    char hunk[4];

		    hunk[3] = '\0';
		    len = (*s++ - ' ') & 077;
		    total += len;
		    if (total > RSTRING(str)->len) {
			len -= total - RSTRING(str)->len;
			total = RSTRING(str)->len;
		    }

		    while (len > 0) {
			int mlen = len > 3 ? 3 : len;

			if (s < send && *s >= ' ')
			    a = (*s++ - ' ') & 077;
			else
			    a = 0;
			if (s < send && *s >= ' ')
			    b = (*s++ - ' ') & 077;
			else
			    b = 0;
			if (s < send && *s >= ' ')
			    c = (*s++ - ' ') & 077;
			else
			    c = 0;
			if (s < send && *s >= ' ')
			    d = (*s++ - ' ') & 077;
			else
			    d = 0;
			hunk[0] = a << 2 | b >> 4;
			hunk[1] = b << 4 | c >> 2;
			hunk[2] = c << 6 | d;
			memcpy(ptr, hunk, mlen);
			ptr += mlen;
			len -= mlen;
		    }
		    if (*s == '\r') s++;
		    if (*s == '\n') s++;
		    else if (s < send && (s+1 == send || s[1] == '\n'))
			s += 2;	/* possible checksum byte */
		}
		
		RSTRING(str)->ptr[total] = '\0';
		RSTRING(str)->len = total;
		rb_ary_push(ary, str);
	    }
	    break;

	  case 'm':
	    {
		VALUE str = rb_str_new(0, (send - s)*3/4);
		char *ptr = RSTRING(str)->ptr;
		int a,b,c,d;
		static int first = 1;
		static int b64_xtable[256];

		if (first) {
		    int i;
		    first = 0;

		    for (i = 0; i < 256; i++) {
			b64_xtable[i] = -1;
		    }
		    for (i = 0; i < 64; i++) {
			b64_xtable[(int)b64_table[i]] = i;
		    }
		}
		for (;;) {
		    while (s[0] == '\r' || s[0] == '\n') { s++; }
		    if ((a = b64_xtable[(int)s[0]]) == -1) break;
		    if ((b = b64_xtable[(int)s[1]]) == -1) break;
		    if ((c = b64_xtable[(int)s[2]]) == -1) break;
		    if ((d = b64_xtable[(int)s[3]]) == -1) break;
		    *ptr++ = a << 2 | b >> 4;
		    *ptr++ = b << 4 | c >> 2;
		    *ptr++ = c << 6 | d;
		    s += 4;
		}
		if (a != -1 && b != -1 && s[2] == '=') {
		    *ptr++ = a << 2 | b >> 4;
		}
		if (a != -1 && b != -1 && c != -1 && s[3] == '=') {
		    *ptr++ = a << 2 | b >> 4;
		    *ptr++ = b << 4 | c >> 2;
		}
		*ptr = '\0';
		RSTRING(str)->len = ptr - RSTRING(str)->ptr;
		rb_ary_push(ary, str);
	    }
	    break;

	  case 'M':
	    {
		VALUE str = rb_str_new(0, send - s);
		char *ptr = RSTRING(str)->ptr;
		int c1, c2;

		while (s < send) {
		    if (*s == '=') {
			if (++s == send) break;
			if (*s != '\n') {
			    if ((c1 = hex2num(*s)) == -1) break;
			    if (++s == send) break;
			    if ((c2 = hex2num(*s)) == -1) break;
			    *ptr++ = c1 << 4 | c2;
			}
		    }
		    else {
			*ptr++ = *s;
		    }
		    s++;
		}
		*ptr = '\0';
		RSTRING(str)->len = ptr - RSTRING(str)->ptr;
		rb_ary_push(ary, str);
	    }
	    break;

	  case '@':
	    s = RSTRING(str)->ptr + len;
	    break;

	  case 'X':
	    if (len > s - RSTRING(str)->ptr)
		rb_raise(rb_eArgError, "X outside of string");
	    s -= len;
	    break;

	  case 'x':
	    if (len > send - s)
		rb_raise(rb_eArgError, "x outside of string");
	    s += len;
	    break;

	  case 'P':
	    if (sizeof(char *) <= send - s) {
		char *t;
		VALUE str = rb_str_new(0, 0);
		memcpy(&t, s, sizeof(char *));
		s += sizeof(char *);
		if (t)
		    rb_str_cat(str, t, len);
		rb_ary_push(ary, str);
	    }
	    break;

	  case 'p':
	    if (len > (send - s) / sizeof(char *))
		len = (send - s) / sizeof(char *);
	    while (len-- > 0) {
		if (send - s < sizeof(char *))
		    break;
		else {
		    char *t;
		    VALUE str = rb_str_new(0, 0);
		    memcpy(&t, s, sizeof(char *));
		    s += sizeof(char *);
		    if (t) {
			rb_str_cat2(str, t);
		    }
		    rb_ary_push(ary, str);
		}
	    }
	    break;

	  case 'w':
	    {
		unsigned long ul = 0;
		unsigned long ulmask = 0xfeL << ((sizeof(unsigned long) - 1) * 8);

		while (len > 0 && s < send) {
		    ul <<= 7;
		    ul |= (*s & 0x7f);
		    if (!(*s++ & 0x80)) {
			rb_ary_push(ary, rb_uint2inum(ul));
			len--;
			ul = 0;
		    }
		    else if (ul & ulmask) {
			VALUE big = rb_uint2big(ul);
			VALUE big128 = rb_uint2big(128);
			while (s < send) {
			    big = rb_big_mul(big, big128);
			    big = rb_big_plus(big, rb_uint2big(*s & 0x7f));
			    if (!(*s++ & 0x80)) {
				rb_ary_push(ary, big);
				len--;
				ul = 0;
				break;
			    }
			}
		    }
		}
	    }
	    break;

	  default:
	    break;
	}
    }

    return ary;
}

#define BYTEWIDTH 8

static int
uv_to_utf8(buf, uv)
    char *buf;
    unsigned long uv;
{
    if (uv <= 0x7f) {
	buf[0] = (char)uv;
	return 1;
    }
    if (uv <= 0x7ff) {
	buf[0] = ((uv>>6)&0xff)|0xc0;
	buf[1] = (uv&0x3f)|0x80;
	return 2;
    }
    if (uv <= 0xffff) {
	buf[0] = ((uv>>12)&0xff)|0xe0;
	buf[1] = ((uv>>6)&0x3f)|0x80;
	buf[2] = (uv&0x3f)|0x80;
	return 3;
    }
    if (uv <= 0x1fffff) {
	buf[0] = ((uv>>18)&0xff)|0xf0;
	buf[1] = ((uv>>12)&0x3f)|0x80;
	buf[2] = ((uv>>6)&0x3f)|0x80;
	buf[3] = (uv&0x3f)|0x80;
	return 4;
    }
    if (uv <= 0x3ffffff) {
	buf[0] = ((uv>>24)&0xff)|0xf8;
	buf[1] = ((uv>>18)&0x3f)|0x80;
	buf[2] = ((uv>>12)&0x3f)|0x80;
	buf[3] = ((uv>>6)&0x3f)|0x80;
	buf[4] = (uv&0x3f)|0x80;
	return 5;
    }
    if (uv <= 0x7fffffff) {
	buf[0] = ((uv>>30)&0xff)|0xfc;
	buf[1] = ((uv>>24)&0x3f)|0x80;
	buf[2] = ((uv>>18)&0x3f)|0x80;
	buf[3] = ((uv>>12)&0x3f)|0x80;
	buf[4] = ((uv>>6)&0x3f)|0x80;
	buf[5] = (uv&0x3f)|0x80;
	return 6;
    }
#if SIZEOF_LONG > 4
    if (uv <= 0xfffffffff) {
#endif
	buf[0] = 0xfe;
	buf[1] = ((uv>>30)&0x3f)|0x80;
	buf[2] = ((uv>>24)&0x3f)|0x80;
	buf[3] = ((uv>>18)&0x3f)|0x80;
	buf[4] = ((uv>>12)&0x3f)|0x80;
	buf[5] = ((uv>>6)&0x3f)|0x80;
	buf[6] = (uv&0x3f)|0x80;
	return 7;
#if SIZEOF_LONG > 4
    }
    rb_raise(rb_eArgError, "uv_to_utf8(); too big value");
#endif
}

static unsigned long
utf8_to_uv(p, lenp)
    char *p;
    int *lenp;
{
    int c = (*p++)&0xff;
    unsigned long uv;
    int n = 1;

    if (c < 0xc0) n = 1;
    else if (c < 0xe0) n = 2;
    else if (c < 0xf0) n = 3;
    else if (c < 0xf8) n = 4;
    else if (c < 0xfc) n = 5;
    else if (c < 0xfe) n = 6;
    else if (c == 0xfe) n = 7;
    if (n > *lenp) return 0;
    *lenp = n--;

    uv = c;
    if (n != 0) {
	uv &= (1<<(BYTEWIDTH-2-n)) - 1;
	while (n--) {
	    uv = uv << 6 | *p++ & ((1<<6)-1);
	}
    }
    return uv;
}

void
Init_pack()
{
    rb_define_method(rb_cArray, "pack", pack_pack, 1);
    rb_define_method(rb_cString, "unpack", pack_unpack, 1);
}
