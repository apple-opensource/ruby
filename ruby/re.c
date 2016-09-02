/**********************************************************************

  re.c -

  $Author: melville $
  created at: Mon Aug  9 18:24:49 JST 1993

  Copyright (C) 1993-2000 Yukihiro Matsumoto

**********************************************************************/

#include "ruby.h"
#include "re.h"

static VALUE rb_eRegexpError;

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

#if 'a' == 97   /* it's ascii */
static const char casetable[] = {
        '\000', '\001', '\002', '\003', '\004', '\005', '\006', '\007',
        '\010', '\011', '\012', '\013', '\014', '\015', '\016', '\017',
        '\020', '\021', '\022', '\023', '\024', '\025', '\026', '\027',
        '\030', '\031', '\032', '\033', '\034', '\035', '\036', '\037',
        /* ' '     '!'     '"'     '#'     '$'     '%'     '&'     ''' */
        '\040', '\041', '\042', '\043', '\044', '\045', '\046', '\047',
        /* '('     ')'     '*'     '+'     ','     '-'     '.'     '/' */
        '\050', '\051', '\052', '\053', '\054', '\055', '\056', '\057',
        /* '0'     '1'     '2'     '3'     '4'     '5'     '6'     '7' */
        '\060', '\061', '\062', '\063', '\064', '\065', '\066', '\067',
        /* '8'     '9'     ':'     ';'     '<'     '='     '>'     '?' */
        '\070', '\071', '\072', '\073', '\074', '\075', '\076', '\077',
        /* '@'     'A'     'B'     'C'     'D'     'E'     'F'     'G' */
        '\100', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
        /* 'H'     'I'     'J'     'K'     'L'     'M'     'N'     'O' */
        '\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
        /* 'P'     'Q'     'R'     'S'     'T'     'U'     'V'     'W' */
        '\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
        /* 'X'     'Y'     'Z'     '['     '\'     ']'     '^'     '_' */
        '\170', '\171', '\172', '\133', '\134', '\135', '\136', '\137',
        /* '`'     'a'     'b'     'c'     'd'     'e'     'f'     'g' */
        '\140', '\141', '\142', '\143', '\144', '\145', '\146', '\147',
        /* 'h'     'i'     'j'     'k'     'l'     'm'     'n'     'o' */
        '\150', '\151', '\152', '\153', '\154', '\155', '\156', '\157',
        /* 'p'     'q'     'r'     's'     't'     'u'     'v'     'w' */
        '\160', '\161', '\162', '\163', '\164', '\165', '\166', '\167',
        /* 'x'     'y'     'z'     '{'     '|'     '}'     '~' */
        '\170', '\171', '\172', '\173', '\174', '\175', '\176', '\177',
        '\200', '\201', '\202', '\203', '\204', '\205', '\206', '\207',
        '\210', '\211', '\212', '\213', '\214', '\215', '\216', '\217',
        '\220', '\221', '\222', '\223', '\224', '\225', '\226', '\227',
        '\230', '\231', '\232', '\233', '\234', '\235', '\236', '\237',
        '\240', '\241', '\242', '\243', '\244', '\245', '\246', '\247',
        '\250', '\251', '\252', '\253', '\254', '\255', '\256', '\257',
        '\260', '\261', '\262', '\263', '\264', '\265', '\266', '\267',
        '\270', '\271', '\272', '\273', '\274', '\275', '\276', '\277',
        '\300', '\301', '\302', '\303', '\304', '\305', '\306', '\307',
        '\310', '\311', '\312', '\313', '\314', '\315', '\316', '\317',
        '\320', '\321', '\322', '\323', '\324', '\325', '\326', '\327',
        '\330', '\331', '\332', '\333', '\334', '\335', '\336', '\337',
        '\340', '\341', '\342', '\343', '\344', '\345', '\346', '\347',
        '\350', '\351', '\352', '\353', '\354', '\355', '\356', '\357',
        '\360', '\361', '\362', '\363', '\364', '\365', '\366', '\367',
        '\370', '\371', '\372', '\373', '\374', '\375', '\376', '\377',
};
#else
>>> "You lose. You will need a translation table for your character set." <<<
#endif

#define MIN(a,b) (((a)>(b))?(b):(a))

int
rb_memcmp(p1, p2, len)
    char *p1, *p2;
    long len;
{
    int tmp;

    if (!ruby_ignorecase) {
	return memcmp(p1, p2, len);
    }

    while (len--) {
	if (tmp = casetable[(unsigned)*p1++] - casetable[(unsigned)*p2++])
	    return tmp;
    }
    return 0;
}

#define REG_CASESTATE  FL_USER0
#define KCODE_NONE  0
#define KCODE_EUC   FL_USER1
#define KCODE_SJIS  FL_USER2
#define KCODE_UTF8  FL_USER3
#define KCODE_FIXED FL_USER4
#define KCODE_MASK (KCODE_EUC|KCODE_SJIS|KCODE_UTF8)

static int reg_kcode = DEFAULT_KCODE;

static void
kcode_euc(re)
    struct RRegexp *re;
{
    FL_UNSET(re, KCODE_MASK);
    FL_SET(re, KCODE_EUC);
    FL_SET(re, KCODE_FIXED);
}

static void
kcode_sjis(re)
    struct RRegexp *re;
{
    FL_UNSET(re, KCODE_MASK);
    FL_SET(re, KCODE_SJIS);
    FL_SET(re, KCODE_FIXED);
}

static void
kcode_utf8(re)
    struct RRegexp *re;
{
    FL_UNSET(re, KCODE_MASK);
    FL_SET(re, KCODE_UTF8);
    FL_SET(re, KCODE_FIXED);
}

static void
kcode_none(re)
    struct RRegexp *re;
{
    FL_UNSET(re, KCODE_MASK);
    FL_SET(re, KCODE_FIXED);
}

static int curr_kcode;

static void
kcode_set_option(re)
    VALUE re;
{
    if (!FL_TEST(re, KCODE_FIXED)) return;

    curr_kcode = RBASIC(re)->flags & KCODE_MASK;
    if (reg_kcode == curr_kcode) return;
    switch (curr_kcode) {
      case KCODE_NONE:
	re_mbcinit(MBCTYPE_ASCII);
	break;
      case KCODE_EUC:
	re_mbcinit(MBCTYPE_EUC);
	break;
      case KCODE_SJIS:
	re_mbcinit(MBCTYPE_SJIS);
	break;
      case KCODE_UTF8:
	re_mbcinit(MBCTYPE_UTF8);
	break;
    }
}	  

static void
kcode_reset_option()
{
    if (reg_kcode == curr_kcode) return;
    switch (reg_kcode) {
      case KCODE_NONE:
	re_mbcinit(MBCTYPE_ASCII);
	break;
      case KCODE_EUC:
	re_mbcinit(MBCTYPE_EUC);
	break;
      case KCODE_SJIS:
	re_mbcinit(MBCTYPE_SJIS);
	break;
      case KCODE_UTF8:
	re_mbcinit(MBCTYPE_UTF8);
	break;
    }
}

int
rb_reg_mbclen2(c, re)
    unsigned int c;
    VALUE re;
{
    int len;

    if (!FL_TEST(re, KCODE_FIXED))
	return mbclen(c);
    kcode_set_option(re);
    len = mbclen(c);
    kcode_reset_option();
    return len;
}

static void
rb_reg_check(re)
    VALUE re;
{
    if (!RREGEXP(re)->ptr || !RREGEXP(re)->str) {
	rb_raise(rb_eTypeError, "uninitialized Regexp");
    }
}

extern int ruby_in_compile;

static void
rb_reg_expr_str(str, s, len)
    VALUE str;
    const char *s;
    int len;
{
    const char *p, *pend;
    int need_escape = 0;

    p = s; pend = p + len; 
    while (p<pend) {
	if (*p == '/' || (!ISPRINT(*p) && !ismbchar(*p))) {
	    need_escape = 1;
	    break;
	}
	p += mbclen(*p);
    }
    if (!need_escape) {
	rb_str_cat(str, s, len);
    }
    else {
	p = s; 
	while (p<pend) {
	    if (*p == '\\') {
		rb_str_cat(str, p, 1);
		p++;
	    	rb_str_cat(str, p, mbclen(*p));
		p += mbclen(*p);
		continue;
	    }
	    else if (*p == '/') {
		char c = '\\';
		rb_str_cat(str, &c, 1);
		rb_str_cat(str, p, 1);
	    }
	    else if (ismbchar(*p)) {
	    	rb_str_cat(str, p, mbclen(*p));
		p += mbclen(*p);
		continue;
	    }
	    else if (ISPRINT(*p)) {
		rb_str_cat(str, p, 1);
	    }
	    else if (!ISSPACE(*p)) {
		char b[8];

		sprintf(b, "\\%03o", *p & 0377);
		rb_str_cat(str, b, 4);
	    }
	    else {
		rb_str_cat(str, p, 1);
	    }
	    p++;
	}
    }
}

static VALUE
rb_reg_desc(s, len, re)
    const char *s;
    int len;
    VALUE re;
{
    VALUE str = rb_str_new2("/");

    rb_reg_expr_str(str, s, len);
    rb_str_cat2(str, "/");
    if (re) {
	rb_reg_check(re);
	/* /p is obsolete; to be removed */
	if ((RREGEXP(re)->ptr->options & RE_OPTION_POSIXLINE) == RE_OPTION_POSIXLINE)
	    rb_str_cat2(str, "p");
	else if (RREGEXP(re)->ptr->options & RE_OPTION_MULTILINE)
	    rb_str_cat2(str, "m");
	if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE)
	    rb_str_cat2(str, "i");
	if (RREGEXP(re)->ptr->options & RE_OPTION_EXTENDED)
	    rb_str_cat2(str, "x");
	
	if (FL_TEST(re, KCODE_FIXED)) {
	    switch ((RBASIC(re)->flags & KCODE_MASK)) {
	      case KCODE_NONE:
		rb_str_cat2(str, "n");
		break;
	      case KCODE_EUC:
		rb_str_cat2(str, "e");
		break;
	      case KCODE_SJIS:
		rb_str_cat2(str, "s");
		break;
	      case KCODE_UTF8:
		rb_str_cat2(str, "u");
		break;
	    }
	}
    }
    OBJ_INFECT(str, re);
    return str;
}

static VALUE
rb_reg_source(re)
    VALUE re;
{
    VALUE str;

    rb_reg_check(re);
    str = rb_str_new(RREGEXP(re)->str,RREGEXP(re)->len);
    if (OBJ_TAINTED(re)) OBJ_TAINT(str);
    return str;
}

static VALUE
rb_reg_inspect(re)
    VALUE re;
{
    rb_reg_check(re);
    return rb_reg_desc(RREGEXP(re)->str, RREGEXP(re)->len, re);
}

static void
rb_reg_raise(s, len, err, re)
    const char *s;
    int len;
    const char *err;
    VALUE re;
{
    VALUE desc = rb_reg_desc(s, len, re);

    if (ruby_in_compile)
	rb_compile_error("%s: %s", err, RSTRING(desc)->ptr);
    else
	rb_raise(rb_eRegexpError, "%s: %s", err, RSTRING(desc)->ptr);
}

static VALUE
rb_reg_casefold_p(re)
    VALUE re;
{
    rb_reg_check(re);
    if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE) return Qtrue;
    return Qfalse;
}

static VALUE
rb_reg_kcode_m(re)
    VALUE re;
{
    char *kcode;

    if (FL_TEST(re, KCODE_FIXED)) {
	switch (RBASIC(re)->flags & KCODE_MASK) {
	  case KCODE_NONE:
	    kcode = "none"; break;
	  case KCODE_EUC:
	    kcode = "euc"; break;
	  case KCODE_SJIS:
	    kcode = "sjis"; break;
	  case KCODE_UTF8:
	    kcode = "utf8"; break;
	  default:
	    rb_bug("unknow kcode - should not happen");
	    break;
	}
	return rb_str_new2(kcode);
    }
    return Qnil;
}

static Regexp*
make_regexp(s, len, flag)
    const char *s;
    int len, flag;
{
    Regexp *rp;
    char *err;

    /* Handle escaped characters first. */

    /* Build a copy of the string (in dest) with the
       escaped characters translated,  and generate the regex
       from that.
    */

    rp = ALLOC(Regexp);
    MEMZERO((char *)rp, Regexp, 1);
    rp->buffer = ALLOC_N(char, 16);
    rp->allocated = 16;
    rp->fastmap = ALLOC_N(char, 256);
    if (flag) {
	rp->options = flag;
    }
    err = re_compile_pattern(s, len, rp);
    if (err != NULL) {
	rb_reg_raise(s, len, err, 0);
    }

    return rp;
}

static VALUE rb_cMatch;

static VALUE
match_alloc()
{
    NEWOBJ(match, struct RMatch);
    OBJSETUP(match, rb_cMatch, T_MATCH);

    match->str = 0;
    match->regs = 0;
    match->regs = ALLOC(struct re_registers);
    MEMZERO(match->regs, struct re_registers, 1);

    return (VALUE)match;
}

static VALUE
match_clone(match)
    VALUE match;
{
    NEWOBJ(clone, struct RMatch);
    CLONESETUP(clone, match);

    clone->str = RMATCH(match)->str;
    clone->regs = 0;

    clone->regs = ALLOC(struct re_registers);
    clone->regs->allocated = 0;
    re_copy_registers(clone->regs, RMATCH(match)->regs);

    return (VALUE)clone;
}

static VALUE
match_size(match)
    VALUE match;
{
    return INT2FIX(RMATCH(match)->regs->num_regs);
}

static VALUE
match_offset(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return rb_assoc_new(Qnil, Qnil);

    return rb_assoc_new(INT2FIX(RMATCH(match)->regs->beg[i]),
			INT2FIX(RMATCH(match)->regs->end[i]));
}

static VALUE
match_begin(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return Qnil;

    return INT2FIX(RMATCH(match)->regs->beg[i]);
}

static VALUE
match_end(match, n)
    VALUE match, n;
{
    int i = NUM2INT(n);

    if (i < 0 || RMATCH(match)->regs->num_regs <= i)
	rb_raise(rb_eIndexError, "index %d out of matches", i);

    if (RMATCH(match)->regs->beg[i] < 0)
	return Qnil;

    return INT2FIX(RMATCH(match)->regs->end[i]);
}

#define MATCH_BUSY FL_USER2

void
rb_match_busy(match)
    VALUE match;
{
    FL_SET(match, MATCH_BUSY);
}

int ruby_ignorecase;
static int may_need_recompile;

static void
rb_reg_prepare_re(re)
    VALUE re;
{
    int need_recompile = 0;
    int state;

    rb_reg_check(re);
    state = FL_TEST(re, REG_CASESTATE);
    /* ignorecase status */
    if (ruby_ignorecase && !state) {
	FL_SET(re, REG_CASESTATE);
	RREGEXP(re)->ptr->options |= RE_OPTION_IGNORECASE;
	need_recompile = 1;
    }
    if (!ruby_ignorecase && state) {
	FL_UNSET(re, REG_CASESTATE);
	RREGEXP(re)->ptr->options &= ~RE_OPTION_IGNORECASE;
	need_recompile = 1;
    }

    if (!FL_TEST(re, KCODE_FIXED) &&
	(RBASIC(re)->flags & KCODE_MASK) != reg_kcode) {
	need_recompile = 1;
	RBASIC(re)->flags &= ~KCODE_MASK;
	RBASIC(re)->flags |= reg_kcode;
    }

    if (need_recompile) {
	char *err;

	if (FL_TEST(re, KCODE_FIXED))
	    kcode_set_option(re);
	rb_reg_check(re);
	RREGEXP(re)->ptr->fastmap_accurate = 0;
	err = re_compile_pattern(RREGEXP(re)->str, RREGEXP(re)->len, RREGEXP(re)->ptr);
	if (err != NULL) {
	    rb_reg_raise(RREGEXP(re)->str, RREGEXP(re)->len, err, re);
	}
    }
}

int
rb_reg_adjust_startpos(re, str, pos, reverse)
    VALUE re, str;
    int pos, reverse;
{
    int range;

    rb_reg_check(re);
    if (may_need_recompile) rb_reg_prepare_re(re);

    if (FL_TEST(re, KCODE_FIXED))
	kcode_set_option(re);
    else if (reg_kcode != curr_kcode)
	kcode_reset_option();

    if (reverse) {
	range = -pos;
    }
    else {
	range = RSTRING(str)->len - pos;
    }
    return re_adjust_startpos(RREGEXP(re)->ptr,
			      RSTRING(str)->ptr, RSTRING(str)->len,
			      pos, range);
}

int
rb_reg_search(re, str, pos, reverse)
    VALUE re, str;
    int pos, reverse;
{
    int result;
    VALUE match;
    static struct re_registers regs;
    int range;

    if (pos > RSTRING(str)->len) {
	rb_backref_set(Qnil);
	return -1;
    }

    rb_reg_check(re);
    if (may_need_recompile) rb_reg_prepare_re(re);

    if (FL_TEST(re, KCODE_FIXED))
	kcode_set_option(re);
    else if (reg_kcode != curr_kcode)
	kcode_reset_option();

    if (reverse) {
	range = -pos;
    }
    else {
	range = RSTRING(str)->len - pos;
    }
    result = re_search(RREGEXP(re)->ptr,RSTRING(str)->ptr,RSTRING(str)->len,
		       pos, range, &regs);

    if (FL_TEST(re, KCODE_FIXED))
	kcode_reset_option();

    if (result == -2) {
	rb_reg_raise(RREGEXP(re)->str, RREGEXP(re)->len,
		  "Stack overflow in regexp matcher", re);
    }

    if (result < 0) {
	rb_backref_set(Qnil);
	return result;
    }

    if (rb_thread_scope_shared_p()) {
	match = Qnil;
    }
    else {
	match = rb_backref_get();
    }
    if (NIL_P(match) || FL_TEST(match, MATCH_BUSY)) {
	match = match_alloc();
    }
    else {
	if (rb_safe_level() >= 3) 
	    OBJ_TAINT(match);
	else
	    FL_UNSET(match, FL_TAINT);
    }

    re_copy_registers(RMATCH(match)->regs, &regs);
    RMATCH(match)->str = rb_str_new4(str);
    rb_backref_set(match);

    OBJ_INFECT(match, re);
    OBJ_INFECT(match, str);
    return result;
}

VALUE
rb_reg_nth_defined(nth, match)
    int nth;
    VALUE match;
{
    if (NIL_P(match)) return Qnil;
    if (nth >= RMATCH(match)->regs->num_regs) {
	return Qnil;
    }
    if (nth < 0) {
	nth += RMATCH(match)->regs->num_regs;
	if (nth <= 0) return Qnil;
    }
    if (RMATCH(match)->BEG(nth) == -1) return Qfalse;
    return Qtrue;
}

VALUE
rb_reg_nth_match(nth, match)
    int nth;
    VALUE match;
{
    VALUE str;
    int start, end, len;

    if (NIL_P(match)) return Qnil;
    if (nth >= RMATCH(match)->regs->num_regs) {
	return Qnil;
    }
    if (nth < 0) {
	nth += RMATCH(match)->regs->num_regs;
	if (nth <= 0) return Qnil;
    }
    start = RMATCH(match)->BEG(nth);
    if (start == -1) return Qnil;
    end = RMATCH(match)->END(nth);
    len = end - start;
    str = rb_str_new(RSTRING(RMATCH(match)->str)->ptr + start, len);
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    return str;
}

VALUE
rb_reg_last_match(match)
    VALUE match;
{
    return rb_reg_nth_match(0, match);
}

VALUE
rb_reg_match_pre(match)
    VALUE match;
{
    VALUE str;

    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;
    str = rb_str_new(RSTRING(RMATCH(match)->str)->ptr, RMATCH(match)->BEG(0));
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    return str;
}

VALUE
rb_reg_match_post(match)
    VALUE match;
{
    VALUE str;

    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;
    str = rb_str_new(RSTRING(RMATCH(match)->str)->ptr+RMATCH(match)->END(0),
		     RSTRING(RMATCH(match)->str)->len-RMATCH(match)->END(0));
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    return str;
}

VALUE
rb_reg_match_last(match)
    VALUE match;
{
    int i;

    if (NIL_P(match)) return Qnil;
    if (RMATCH(match)->BEG(0) == -1) return Qnil;

    for (i=RMATCH(match)->regs->num_regs-1; RMATCH(match)->BEG(i) == -1 && i > 0; i--)
	;
    if (i == 0) return Qnil;
    return rb_reg_nth_match(i, match);
}

static VALUE
last_match_getter()
{
    return rb_reg_last_match(rb_backref_get());
}

static VALUE
prematch_getter()
{
    return rb_reg_match_pre(rb_backref_get());
}

static VALUE
postmatch_getter()
{
    return rb_reg_match_post(rb_backref_get());
}

static VALUE
last_paren_match_getter()
{
    return rb_reg_match_last(rb_backref_get());
}

static VALUE
match_to_a(match)
    VALUE match;
{
    struct re_registers *regs = RMATCH(match)->regs;
    VALUE ary = rb_ary_new2(regs->num_regs);
    char *ptr = RSTRING(RMATCH(match)->str)->ptr;
    int i;
    int taint = OBJ_TAINTED(match);
    
    for (i=0; i<regs->num_regs; i++) {
	if (regs->beg[i] == -1) {
	    rb_ary_push(ary, Qnil);
	} else {
	    VALUE str = rb_str_new(ptr+regs->beg[i], regs->end[i]-regs->beg[i]);
	    if (taint) OBJ_TAINT(str);
	    rb_ary_push(ary, str);
	}
    }
    return ary;
}

static VALUE
match_aref(argc, argv, match)
    int argc;
    VALUE *argv;
    VALUE match;
{
    VALUE idx, rest;
    char *ptr;
    int i;

    rb_scan_args(argc, argv, "11", &idx, &rest);

    if (!NIL_P(rest) || !FIXNUM_P(idx) || FIX2INT(idx) < 0) {
	return rb_ary_aref(argc, argv, match_to_a(match));
    }
    return rb_reg_nth_match(FIX2INT(idx), match);
}

static VALUE
match_to_s(match)
    VALUE match;
{
    VALUE str = rb_reg_last_match(match);

    if (NIL_P(str)) str = rb_str_new(0,0);
    if (OBJ_TAINTED(match)) OBJ_TAINT(str);
    if (OBJ_TAINTED(RMATCH(match)->str)) OBJ_TAINT(str);
    return str;
}

static VALUE
match_string(match)
    VALUE match;
{
    return RMATCH(match)->str;	/* str is frozen */
}

VALUE rb_cRegexp;

static void
rb_reg_initialize(obj, s, len, options)
    VALUE obj;
    const char *s;
    int len;
    int options;		/* CASEFOLD  = 1 */
				/* EXTENDED  = 2 */
				/* MULTILINE = 4 */
				/* CODE_NONE = 16 */
				/* CODE_EUC  = 32 */
				/* CODE_SJIS = 48 */
				/* CODE_UTF8 = 64 */
{
    struct RRegexp *re = RREGEXP(obj);

    if (re->ptr) re_free_pattern(re->ptr);
    if (re->str) free(re->str);
    re->ptr = 0;
    re->str = 0;

    switch (options & ~0xf) {
      case 0:
      default:
	FL_SET(re, reg_kcode);
	break;
      case 16:
	kcode_none(re);
	break;
      case 32:
	kcode_euc(re);
	break;
      case 48:
	kcode_sjis(re);
	break;
      case 64:
	kcode_utf8(re);
	break;
    }

    if (options & ~0xf) {
	kcode_set_option((VALUE)re);
    }
    if (ruby_ignorecase) {
	options |= RE_OPTION_IGNORECASE;
	FL_SET(re, REG_CASESTATE);
    }
    re->ptr = make_regexp(s, len, options & 0xf);
    re->str = ALLOC_N(char, len+1);
    memcpy(re->str, s, len);
    re->str[len] = '\0';
    re->len = len;
    if (options & ~0xf) {
	kcode_reset_option();
    }
}

VALUE
rb_reg_new(s, len, options)
    const char *s;
    long len;
    int options;
{
    NEWOBJ(re, struct RRegexp);
    OBJSETUP(re, rb_cRegexp, T_REGEXP);

    re->ptr = 0; re->len = 0; re->str = 0;
    rb_reg_initialize(re, s, len, options);
    return (VALUE)re;
}

static int case_cache;
static int kcode_cache;
static VALUE reg_cache;

VALUE
rb_reg_regcomp(str)
    VALUE str;
{
    if (reg_cache && RREGEXP(reg_cache)->len == RSTRING(str)->len
	&& case_cache == ruby_ignorecase
	&& kcode_cache == reg_kcode
	&& memcmp(RREGEXP(reg_cache)->str, RSTRING(str)->ptr, RSTRING(str)->len) == 0)
	return reg_cache;

    case_cache = ruby_ignorecase;
    kcode_cache = reg_kcode;
    return reg_cache = rb_reg_new(RSTRING(str)->ptr, RSTRING(str)->len,
				  ruby_ignorecase);
}

static int
rb_reg_cur_kcode(re)
    VALUE re;
{
    if (FL_TEST(re, KCODE_FIXED)) {
	return RBASIC(re)->flags & KCODE_MASK;
    }
    return 0;
}

static VALUE
rb_reg_equal(re1, re2)
    VALUE re1, re2;
{
    if (re1 == re2) return Qtrue;
    if (TYPE(re2) != T_REGEXP) return Qfalse;
    rb_reg_check(re1); rb_reg_check(re2);
    if (RREGEXP(re1)->len != RREGEXP(re2)->len) return Qfalse;
    if (memcmp(RREGEXP(re1)->str, RREGEXP(re2)->str, RREGEXP(re1)->len) == 0 &&
	rb_reg_cur_kcode(re1) == rb_reg_cur_kcode(re2) &&
	RREGEXP(re1)->ptr->options == RREGEXP(re2)->ptr->options) {
	return Qtrue;
    }
    return Qfalse;
}

VALUE
rb_reg_match(re, str)
    VALUE re, str;
{
    int start;

    if (NIL_P(str)) {
	rb_backref_set(Qnil);
	return Qnil;
    }
    str = rb_str_to_str(str);
    start = rb_reg_search(re, str, 0, 0);
    if (start < 0) {
	return Qnil;
    }
    return INT2FIX(start);
}

VALUE
rb_reg_match2(re)
    VALUE re;
{
    int start;
    VALUE line = rb_lastline_get();

    if (TYPE(line) != T_STRING) {
	rb_backref_set(Qnil);
	return Qnil;
    }

    start = rb_reg_search(re, line, 0, 0);
    if (start < 0) {
	return Qnil;
    }
    return INT2FIX(start);
}

static VALUE
rb_reg_match_m(re, str)
    VALUE re, str;
{
    VALUE result = rb_reg_match(re, str);

    if (NIL_P(result)) return Qnil;
    result = rb_backref_get();
    rb_match_busy(result);
    return result;
}

static VALUE
rb_reg_initialize_m(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE src;
    int flag = 0;

    if (argc == 0 || argc > 3) {
	rb_raise(rb_eArgError, "wrong # of argument");
    }
    if (argc >= 2) {
	if (FIXNUM_P(argv[1])) flag = FIX2INT(argv[1]);
	else if (RTEST(argv[1])) flag = RE_OPTION_IGNORECASE;
    }
    if (argc == 3) {
	char *kcode = STR2CSTR(argv[2]);

	switch (kcode[0]) {
	  case 'n': case 'N':
	    flag |= 16;
	    break;
	  case 'e': case 'E':
	    flag |= 32;
	    break;
	  case 's': case 'S':
	    flag |= 48;
	    break;
	  case 'u': case 'U':
	    flag |= 64;
	    break;
	  default:
	    break;
	}
    }

    if (OBJ_FROZEN(self)) {
	rb_error_frozen("Regexp");
    }
    src = argv[0];
    if (TYPE(src) == T_REGEXP) {
	rb_reg_check(src);
	rb_reg_initialize(self, RREGEXP(src)->str, RREGEXP(src)->len, flag);
    }
    else {
	char *p;
	int len;

	p = rb_str2cstr(src, &len);
	rb_reg_initialize(self, p, len, flag);
    }
    return self;
}

static VALUE
rb_reg_s_new(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    NEWOBJ(re, struct RRegexp);
    OBJSETUP(re, klass, T_REGEXP);
    re->ptr = 0; re->len = 0; re->str = 0;
    rb_obj_call_init((VALUE)re, argc, argv);
    return (VALUE)re;
}

static VALUE
rb_reg_s_quote(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE str, kcode;
    int kcode_saved = reg_kcode;
    char *s, *send, *t;
    VALUE tmp;
    int len, c;

    rb_scan_args(argc, argv, "11", &str, &kcode);
    if (!NIL_P(kcode)) {
	rb_set_kcode(STR2CSTR(kcode));
	curr_kcode = reg_kcode;
	reg_kcode = kcode_saved;
    }
    s = rb_str2cstr(str, &len);
    send = s + len;
    tmp = rb_str_new(0, len*2);
    t = RSTRING(tmp)->ptr;

    for (; s < send; s++) {
	c = *s;
	if (ismbchar(c)) {
	    int n = mbclen(c);

	    while (n-- && s < send)
		*t++ = *s++;
	    s--;
	    continue;
	}
	switch (c) {
	  case '\t':
	    c = 't';
	    *t++ = '\\';
	    break;
	  case '\f':
	    c = 'f';
	    *t++ = '\\';
	    break;
	  case '\r':
	    c = 'r';
	    *t++ = '\\';
	    break;
	  case '\n':
	    c = 'n';
	    *t++ = '\\';
	    break;
	  case '[': case ']': case '{': case '}':
	  case '(': case ')': case '|': case '-':
	  case '*': case '.': case '\\':
	  case '?': case '+': case '^': case '$':
	  case ' ': case '#':
	    *t++ = '\\';
	    break;
	}
	*t++ = c;
    }
    kcode_reset_option();
    rb_str_resize(tmp, t - RSTRING(tmp)->ptr);
    OBJ_INFECT(tmp, str);
    return tmp;
}

int
rb_kcode()
{
    switch (reg_kcode) {
      case KCODE_EUC:
	return MBCTYPE_EUC;
      case KCODE_SJIS:
	return MBCTYPE_SJIS;
      case KCODE_UTF8:
	return MBCTYPE_UTF8;
      case KCODE_NONE:
	return MBCTYPE_ASCII;
    }
    rb_bug("wrong reg_kcode value (0x%x)", reg_kcode);
}

static int
rb_reg_get_kcode(re)
    VALUE re;
{
    int kcode;

    switch (RBASIC(re)->flags & KCODE_MASK) {
      case KCODE_NONE:
	return 16;
      case KCODE_EUC:
	return 32;
      case KCODE_SJIS:
	return 48;
      case KCODE_UTF8:
	return 64;
      default:
	return 0;
    }
}

int
rb_reg_options(re)
    VALUE re;
{
    int options = 0;

    rb_reg_check(re);
    if (RREGEXP(re)->ptr->options & RE_OPTION_IGNORECASE)
	options |= RE_OPTION_IGNORECASE;
    if ((RREGEXP(re)->ptr->options & RE_OPTION_POSIXLINE) == RE_OPTION_POSIXLINE)
	options |= RE_OPTION_POSIXLINE;
    else if (RREGEXP(re)->ptr->options & RE_OPTION_MULTILINE)
	options |= RE_OPTION_MULTILINE;
    if (RREGEXP(re)->ptr->options & RE_OPTION_EXTENDED)
	options |= RE_OPTION_EXTENDED;
    if (FL_TEST(re, KCODE_FIXED)) {
	options |= rb_reg_get_kcode(re);
    }
    return options;
}

static VALUE
rb_reg_clone(re)
    VALUE re;
{
    NEWOBJ(clone, struct RRegexp);
    CLONESETUP(clone, re);
    rb_reg_check(re);
    clone->ptr = 0; clone->len = 0; clone->str = 0;
    rb_reg_initialize(clone, RREGEXP(re)->str, RREGEXP(re)->len,
		      rb_reg_options(re));
    return (VALUE)clone;
}

VALUE
rb_reg_regsub(str, src, regs)
    VALUE str, src;
    struct re_registers *regs;
{
    VALUE val = 0;
    char *p, *s, *e, c;
    int no;

    p = s = RSTRING(str)->ptr;
    e = s + RSTRING(str)->len;

    while (s < e) {
	char *ss = s;

	c = *s++;
	if (ismbchar(c)) {
	    s += mbclen(c) - 1;
	    continue;
	}
	if (c != '\\' || s == e) continue;

	if (!val) val = rb_str_new(p, ss-p);
	else      rb_str_cat(val, p, ss-p);

	c = *s++;
	p = s;
	switch (c) {
	  case '0': case '1': case '2': case '3': case '4':
	  case '5': case '6': case '7': case '8': case '9':
	    no = c - '0';
	    break;
	  case '&':
	    no = 0;
	    break;

	  case '`':
	    rb_str_cat(val, RSTRING(src)->ptr, BEG(0));
	    continue;

	  case '\'':
	    rb_str_cat(val, RSTRING(src)->ptr+END(0), RSTRING(src)->len-END(0));
	    continue;

	  case '+':
	    no = regs->num_regs-1;
	    while (BEG(no) == -1 && no > 0) no--;
	    if (no == 0) continue;
	    break;

	  case '\\':
	    rb_str_cat(val, s-1, 1);
	    continue;

	  default:
	    rb_str_cat(val, s-2, 2);
	    continue;
	}

	if (no >= 0) {
	    if (no >= regs->num_regs) continue;
	    if (BEG(no) == -1) continue;
	    rb_str_cat(val, RSTRING(src)->ptr+BEG(no), END(no)-BEG(no));
	}
    }

    if (p < e) {
	if (!val) val = rb_str_new(p, e-p);
	else      rb_str_cat(val, p, e-p);
    }
    if (!val) return str;

    return val;
}

const char*
rb_get_kcode()
{
    switch (reg_kcode) {
      case KCODE_SJIS:
	return "SJIS";
      case KCODE_EUC:
	return "EUC";
      case KCODE_UTF8:
	return "UTF8";
      default:
	return "NONE";
    }
}

static VALUE
kcode_getter()
{
    return rb_str_new2(rb_get_kcode());
}

void
rb_set_kcode(code)
    const char *code;
{
    if (code == 0) goto set_no_conversion;

    switch (code[0]) {
      case 'E':
      case 'e':
	reg_kcode = KCODE_EUC;
	re_mbcinit(MBCTYPE_EUC);
	break;
      case 'S':
      case 's':
	reg_kcode = KCODE_SJIS;
	re_mbcinit(MBCTYPE_SJIS);
	break;
      case 'U':
      case 'u':
	reg_kcode = KCODE_UTF8;
	re_mbcinit(MBCTYPE_UTF8);
	break;
      default:
      case 'N':
      case 'n':
      case 'A':
      case 'a':
      set_no_conversion:
	reg_kcode = KCODE_NONE;
	re_mbcinit(MBCTYPE_ASCII);
	break;
    }
}

static void
kcode_setter(val)
    struct RString *val;
{
    may_need_recompile = 1;
    rb_set_kcode(STR2CSTR(val));
}

static VALUE
ignorecase_getter()
{
    return ruby_ignorecase?Qtrue:Qfalse;
}

static void
ignorecase_setter(val)
    VALUE val;
{
    may_need_recompile = 1;
    ruby_ignorecase = RTEST(val);
}

static VALUE
match_getter()
{
    VALUE match = rb_backref_get();

    if (NIL_P(match)) return Qnil;
    rb_match_busy(match);
    return match;
}

static void
match_setter(val)
    VALUE val;
{
    if (!NIL_P(val)) {
	Check_Type(val, T_MATCH);
    }
    rb_backref_set(val);
}

void
Init_Regexp()
{
    rb_eRegexpError = rb_define_class("RegexpError", rb_eStandardError);

    re_set_casetable(casetable);
#if DEFAULT_KCODE == KCODE_EUC
    re_mbcinit(MBCTYPE_EUC);
#else
#if DEFAULT_KCODE == KCODE_SJIS
    re_mbcinit(MBCTYPE_SJIS);
#else
#if DEFAULT_KCODE == KCODE_UTF8
    re_mbcinit(MBCTYPE_UTF8);
#else
    re_mbcinit(MBCTYPE_ASCII);
#endif
#endif
#endif

    rb_define_virtual_variable("$~", match_getter, match_setter);
    rb_define_virtual_variable("$&", last_match_getter, 0);
    rb_define_virtual_variable("$`", prematch_getter, 0);
    rb_define_virtual_variable("$'", postmatch_getter, 0);
    rb_define_virtual_variable("$+", last_paren_match_getter, 0);

    rb_define_virtual_variable("$=", ignorecase_getter, ignorecase_setter);
    rb_define_virtual_variable("$KCODE", kcode_getter, kcode_setter);
    rb_define_virtual_variable("$-K", kcode_getter, kcode_setter);

    rb_cRegexp = rb_define_class("Regexp", rb_cObject);
    rb_define_singleton_method(rb_cRegexp, "new", rb_reg_s_new, -1);
    rb_define_singleton_method(rb_cRegexp, "compile", rb_reg_s_new, -1);
    rb_define_singleton_method(rb_cRegexp, "quote", rb_reg_s_quote, -1);
    rb_define_singleton_method(rb_cRegexp, "escape", rb_reg_s_quote, -1);
    rb_define_singleton_method(rb_cRegexp, "last_match", match_getter, 0);

    rb_define_method(rb_cRegexp, "initialize", rb_reg_initialize_m, -1);
    rb_define_method(rb_cRegexp, "clone", rb_reg_clone, 0);
    rb_define_method(rb_cRegexp, "==", rb_reg_equal, 1);
    rb_define_method(rb_cRegexp, "=~", rb_reg_match, 1);
    rb_define_method(rb_cRegexp, "===", rb_reg_match, 1);
    rb_define_method(rb_cRegexp, "~", rb_reg_match2, 0);
    rb_define_method(rb_cRegexp, "match", rb_reg_match_m, 1);
    rb_define_method(rb_cRegexp, "inspect", rb_reg_inspect, 0);
    rb_define_method(rb_cRegexp, "source", rb_reg_source, 0);
    rb_define_method(rb_cRegexp, "casefold?", rb_reg_casefold_p, 0);
    rb_define_method(rb_cRegexp, "kcode", rb_reg_kcode_m, 0);

    rb_define_const(rb_cRegexp, "IGNORECASE", INT2FIX(RE_OPTION_IGNORECASE));
    rb_define_const(rb_cRegexp, "EXTENDED", INT2FIX(RE_OPTION_EXTENDED));
    rb_define_const(rb_cRegexp, "MULTILINE", INT2FIX(RE_OPTION_MULTILINE));

    rb_global_variable(&reg_cache);

    rb_cMatch  = rb_define_class("MatchData", rb_cObject);
    rb_define_global_const("MatchingData", rb_cMatch);
    rb_undef_method(CLASS_OF(rb_cMatch), "new");

    rb_define_method(rb_cMatch, "clone", match_clone, 0);
    rb_define_method(rb_cMatch, "size", match_size, 0);
    rb_define_method(rb_cMatch, "length", match_size, 0);
    rb_define_method(rb_cMatch, "offset", match_offset, 1);
    rb_define_method(rb_cMatch, "begin", match_begin, 1);
    rb_define_method(rb_cMatch, "end", match_end, 1);
    rb_define_method(rb_cMatch, "to_a", match_to_a, 0);
    rb_define_method(rb_cMatch, "[]", match_aref, -1);
    rb_define_method(rb_cMatch, "pre_match", rb_reg_match_pre, 0);
    rb_define_method(rb_cMatch, "post_match", rb_reg_match_post, 0);
    rb_define_method(rb_cMatch, "to_s", match_to_s, 0);
    rb_define_method(rb_cMatch, "string", match_string, 0);
    rb_define_method(rb_cMatch, "inspect", rb_any_to_s, 0);
}
