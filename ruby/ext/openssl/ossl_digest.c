/*
 * $Id: ossl_digest.c,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
 * 'OpenSSL for Ruby' project
 * Copyright (C) 2001-2002  Michal Rokos <m.rokos@sh.cvut.cz>
 * All rights reserved.
 */
/*
 * This program is licenced under the same licence as Ruby.
 * (See the file 'LICENCE'.)
 */
#include "ossl.h"

#define GetDigest(obj, ctx) do { \
    Data_Get_Struct(obj, EVP_MD_CTX, ctx); \
    if (!ctx) { \
	ossl_raise(rb_eRuntimeError, "Digest CTX wasn't initialized!"); \
    } \
} while (0)
#define SafeGetDigest(obj, ctx) do { \
    OSSL_Check_Kind(obj, cDigest); \
    GetDigest(obj, ctx); \
} while (0)

/*
 * Classes
 */
VALUE mDigest;
VALUE cDigest;
VALUE eDigestError;

/*
 * Public
 */
const EVP_MD *
GetDigestPtr(VALUE obj)
{
    EVP_MD_CTX *ctx;

    SafeGetDigest(obj, ctx);

    return EVP_MD_CTX_md(ctx); /*== ctx->digest*/
}

/*
 * Private
 */
static VALUE
ossl_digest_alloc(VALUE klass)
{
    EVP_MD_CTX *ctx;
    VALUE obj;

    ctx = EVP_MD_CTX_create();
    if (ctx == NULL)
	ossl_raise(rb_eRuntimeError, "EVP_MD_CTX_create() failed");
    obj = Data_Wrap_Struct(klass, 0, EVP_MD_CTX_destroy, ctx);
	
    return obj;
}
DEFINE_ALLOC_WRAPPER(ossl_digest_alloc)

VALUE ossl_digest_update(VALUE, VALUE);

static VALUE
ossl_digest_initialize(int argc, VALUE *argv, VALUE self)
{
    EVP_MD_CTX *ctx;
    const EVP_MD *md;
    char *name;
    VALUE type, data;

    GetDigest(self, ctx);

    rb_scan_args(argc, argv, "11", &type, &data);
    name = StringValuePtr(type);
    if (!NIL_P(data)) StringValue(data);
    
    md = EVP_get_digestbyname(name);
    if (!md) {
	ossl_raise(rb_eRuntimeError, "Unsupported digest algorithm (%s).", name);
    }
    EVP_DigestInit(ctx, md);
    
    if (!NIL_P(data)) return ossl_digest_update(self, data);
    return self;
}

static VALUE
ossl_digest_copy(VALUE self, VALUE other)
{
    EVP_MD_CTX *ctx1, *ctx2;
    
    rb_check_frozen(self);
    if (self == other) return self;

    GetDigest(self, ctx1);
    SafeGetDigest(other, ctx2);

    if (!EVP_MD_CTX_copy(ctx1, ctx2)) {
	ossl_raise(eDigestError, NULL);
    }
    return self;
}

static VALUE
ossl_digest_reset(VALUE self)
{
    EVP_MD_CTX *ctx;

    GetDigest(self, ctx);
    EVP_DigestInit(ctx, EVP_MD_CTX_md(ctx));

    return self;
}

VALUE
ossl_digest_update(VALUE self, VALUE data)
{
    EVP_MD_CTX *ctx;

    GetDigest(self, ctx);
    StringValue(data);
    EVP_DigestUpdate(ctx, RSTRING(data)->ptr, RSTRING(data)->len);

    return self;
}

static void
digest_final(EVP_MD_CTX *ctx, char **buf, int *buf_len)
{
    EVP_MD_CTX final;

    if (!EVP_MD_CTX_copy(&final, ctx)) {
	ossl_raise(eDigestError, NULL);
    }
    if (!(*buf = OPENSSL_malloc(EVP_MD_CTX_size(&final)))) {
	ossl_raise(eDigestError, "Cannot allocate mem for digest");
    }
    EVP_DigestFinal(&final, *buf, buf_len);
    EVP_MD_CTX_cleanup(&final);
}

static VALUE
ossl_digest_digest(VALUE self)
{
    EVP_MD_CTX *ctx;
    char *buf;
    int buf_len;
    VALUE digest;
	
    GetDigest(self, ctx);
    digest_final(ctx, &buf, &buf_len);
    digest = rb_str_new(buf, buf_len);
    OPENSSL_free(buf);
	
    return digest;
}

static VALUE
ossl_digest_hexdigest(VALUE self)
{
    EVP_MD_CTX *ctx;
    char *buf, *hexbuf;
    int buf_len;
    VALUE hexdigest;

    GetDigest(self, ctx);
    digest_final(ctx, &buf, &buf_len);
    if (string2hex(buf, buf_len, &hexbuf, NULL) != 2 * buf_len) {
	OPENSSL_free(buf);
	ossl_raise(eDigestError, "Memory alloc error");
    }
    hexdigest = rb_str_new(hexbuf, 2 * buf_len);
    OPENSSL_free(buf);
    OPENSSL_free(hexbuf);

    return hexdigest;
}

static VALUE
ossl_digest_s_digest(VALUE klass, VALUE str, VALUE data)
{
    VALUE obj =
#if HAVE_RB_DEFINE_ALLOC_FUNC
	rb_class_new_instance(1, &str, klass);
#else
	ossl_digest_alloc_wrapper(1, &str, klass);
#endif

    ossl_digest_update(obj, data);

    return ossl_digest_digest(obj);
}

static VALUE
ossl_digest_s_hexdigest(VALUE klass, VALUE str, VALUE data)
{
    VALUE obj =
#if HAVE_RB_DEFINE_ALLOC_FUNC
	rb_class_new_instance(1, &str, klass);
#else
	ossl_digest_alloc_wrapper(1, &str, klass);
#endif

    ossl_digest_update(obj, data);

    return ossl_digest_hexdigest(obj);
}

static VALUE
ossl_digest_equal(VALUE self, VALUE other)
{
    EVP_MD_CTX *ctx;
    VALUE str1, str2;

    GetDigest(self, ctx);
    if (rb_obj_is_kind_of(other, cDigest) == Qtrue) {
	str2 = ossl_digest_digest(other);
    } else {
	StringValue(other);
	str2 = other;
    }
    if (RSTRING(str2)->len == EVP_MD_CTX_size(ctx)) {
	str1 = ossl_digest_digest(self);
    } else {
	str1 = ossl_digest_hexdigest(self);
    }
    if (RSTRING(str1)->len == RSTRING(str2)->len
	&& rb_str_cmp(str1, str2) == 0) {
	return Qtrue;
    }

    return Qfalse;
}

static VALUE
ossl_digest_name(VALUE self)
{
    EVP_MD_CTX *ctx;

    GetDigest(self, ctx);

    return rb_str_new2(EVP_MD_name(EVP_MD_CTX_md(ctx)));
}

static VALUE
ossl_digest_size(VALUE self)
{
    EVP_MD_CTX *ctx;

    GetDigest(self, ctx);

    return INT2NUM(EVP_MD_CTX_size(ctx));
}

/*
 * INIT
 */
void
Init_ossl_digest()
{
    mDigest = rb_define_module_under(mOSSL, "Digest");
	
    eDigestError = rb_define_class_under(mDigest, "DigestError", eOSSLError);
	
    cDigest = rb_define_class_under(mDigest, "Digest", rb_cObject);
	
    rb_define_alloc_func(cDigest, ossl_digest_alloc);
    rb_define_singleton_method(cDigest, "digest", ossl_digest_s_digest, 2);
    rb_define_singleton_method(cDigest, "hexdigest", ossl_digest_s_hexdigest, 2);
	
    rb_define_method(cDigest, "initialize", ossl_digest_initialize, -1);
    rb_define_method(cDigest, "reset", ossl_digest_reset, 0);
    
    rb_define_copy_func(cDigest, ossl_digest_copy);
    
    rb_define_method(cDigest, "digest", ossl_digest_digest, 0);
    rb_define_method(cDigest, "hexdigest", ossl_digest_hexdigest, 0);
    rb_define_alias(cDigest, "inspect", "hexdigest");
    rb_define_alias(cDigest, "to_s", "hexdigest");
    
    rb_define_method(cDigest, "update", ossl_digest_update, 1);
    rb_define_alias(cDigest, "<<", "update");
    
    rb_define_method(cDigest, "==", ossl_digest_equal, 1);
    
    rb_define_method(cDigest, "name", ossl_digest_name, 0);
    rb_define_method(cDigest, "size", ossl_digest_size, 0);
}
