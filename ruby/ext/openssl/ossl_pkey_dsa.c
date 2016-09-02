/*
 * $Id: ossl_pkey_dsa.c,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
 * 'OpenSSL for Ruby' project
 * Copyright (C) 2001-2002  Michal Rokos <m.rokos@sh.cvut.cz>
 * All rights reserved.
 */
/*
 * This program is licenced under the same licence as Ruby.
 * (See the file 'LICENCE'.)
 */
#if !defined(OPENSSL_NO_DSA)

#include "ossl.h"

#define GetPKeyDSA(obj, pkey) do { \
    GetPKey(obj, pkey); \
    if (EVP_PKEY_type(pkey->type) != EVP_PKEY_DSA) { /* PARANOIA? */ \
	ossl_raise(rb_eRuntimeError, "THIS IS NOT A DSA!"); \
    } \
} while (0)

#define DSA_PRIVATE(dsa) ((dsa)->priv_key)

/*
 * Classes
 */
VALUE cDSA;
VALUE eDSAError;

/*
 * Public
 */
static VALUE
dsa_instance(VALUE klass, DSA *dsa)
{
    EVP_PKEY *pkey;
    VALUE obj;
	
    if (!dsa) {
	return Qfalse;
    }
    if (!(pkey = EVP_PKEY_new())) {
	return Qfalse;
    }
    if (!EVP_PKEY_assign_DSA(pkey, dsa)) {
	EVP_PKEY_free(pkey);
	return Qfalse;
    }
    WrapPKey(klass, obj, pkey);

    return obj;
}

VALUE
ossl_dsa_new(EVP_PKEY *pkey)
{
    VALUE obj;

    if (!pkey) {
	obj = dsa_instance(cDSA, DSA_new());
    } else {
	if (EVP_PKEY_type(pkey->type) != EVP_PKEY_DSA) {
	    ossl_raise(rb_eTypeError, "Not a DSA key!");
	}
	WrapPKey(cDSA, obj, pkey);
    }
    if (obj == Qfalse) {
	ossl_raise(eDSAError, NULL);
    }

    return obj;
}

/*
 * Private
 */
/*
 * CB for yielding when generating DSA params
 */
static void
ossl_dsa_generate_cb(int p, int n, void *arg)
{
    VALUE ary;

    ary = rb_ary_new2(2);
    rb_ary_store(ary, 0, INT2NUM(p));
    rb_ary_store(ary, 1, INT2NUM(n));
	
    rb_yield(ary);
}

static DSA *
dsa_generate(int size)
{
    DSA *dsa;
    unsigned char seed[20];
    int seed_len = 20, counter;
    unsigned long h;
    void (*cb)(int, int, void *) = NULL;

    if (!RAND_bytes(seed, seed_len)) {
	return 0;
    }
    if (rb_block_given_p()) {
	cb = ossl_dsa_generate_cb;
    }
    dsa = DSA_generate_parameters(size, seed, seed_len, &counter, &h, cb, NULL);
    if(!dsa) { /* arg to cb = NULL */
	return 0;
    }
    if (!DSA_generate_key(dsa)) {
	DSA_free(dsa);
	return 0;
    }

    return dsa;
}

static VALUE
ossl_dsa_s_generate(VALUE klass, VALUE size)
{
    DSA *dsa = dsa_generate(FIX2INT(size)); /* err handled by dsa_instance */
    VALUE obj = dsa_instance(klass, dsa);

    if (obj == Qfalse) {
	DSA_free(dsa);
	ossl_raise(eDSAError, NULL);
    }

    return obj;
}

static VALUE
ossl_dsa_initialize(int argc, VALUE *argv, VALUE self)
{
    EVP_PKEY *pkey;
    DSA *dsa;
    BIO *in;
    char *passwd = NULL;
    VALUE buffer, pass;
	
    GetPKey(self, pkey);
    rb_scan_args(argc, argv, "11", &buffer, &pass);
    if (FIXNUM_P(buffer)) {
	if (!(dsa = dsa_generate(FIX2INT(buffer)))) {
	    ossl_raise(eDSAError, NULL);
	}
    } else {
	StringValue(buffer);
	if (!NIL_P(pass)) {
	    passwd = StringValuePtr(pass);
	}
	in = BIO_new_mem_buf(RSTRING(buffer)->ptr, RSTRING(buffer)->len);
	if (!in){
	    ossl_raise(eDSAError, NULL);
	}

	dsa = PEM_read_bio_DSAPrivateKey(in, NULL, ossl_pem_passwd_cb, passwd);
	if (!dsa) {
	    BIO_reset(in);

	    dsa = PEM_read_bio_DSAPublicKey(in, NULL, NULL, NULL);
	}
	if (!dsa) {
	    BIO_reset(in);

	    dsa = PEM_read_bio_DSA_PUBKEY(in, NULL, NULL, NULL);
	}
	if (!dsa) {
	    BIO_free(in);
	    ossl_raise(eDSAError, "Neither PUB key nor PRIV key:");
	}
	BIO_free(in);
    }
    if (!EVP_PKEY_assign_DSA(pkey, dsa)) {
	DSA_free(dsa);
	ossl_raise(eDSAError, NULL);
    }

    return self;
}

static VALUE
ossl_dsa_is_public(VALUE self)
{
    EVP_PKEY *pkey;

    GetPKeyDSA(self, pkey);

    /*
     * Do we need to check dsap->dsa->public_pkey?
     * return Qtrue;
     */
    return (pkey->pkey.dsa->pub_key) ? Qtrue : Qfalse;
}

static VALUE
ossl_dsa_is_private(VALUE self)
{
    EVP_PKEY *pkey;
	
    GetPKeyDSA(self, pkey);
	
    return (DSA_PRIVATE(pkey->pkey.dsa)) ? Qtrue : Qfalse;
}

static VALUE
ossl_dsa_export(int argc, VALUE *argv, VALUE self)
{
    EVP_PKEY *pkey;
    BIO *out;
    BUF_MEM *buf;
    const EVP_CIPHER *ciph = NULL;
    char *passwd = NULL;
    VALUE cipher, pass, str;

    GetPKeyDSA(self, pkey);
    rb_scan_args(argc, argv, "02", &cipher, &pass);
    if (!NIL_P(cipher)) {
	ciph = GetCipherPtr(cipher);
	if (!NIL_P(pass)) {
	    passwd = StringValuePtr(pass);
	}
    }
    if (!(out = BIO_new(BIO_s_mem()))) {
	ossl_raise(eDSAError, NULL);
    }
    if (DSA_PRIVATE(pkey->pkey.dsa)) {
	if (!PEM_write_bio_DSAPrivateKey(out, pkey->pkey.dsa, ciph,
					 NULL, 0, ossl_pem_passwd_cb, passwd)){
	    BIO_free(out);
	    ossl_raise(eDSAError, NULL);
	}
    } else {
	if (!PEM_write_bio_DSAPublicKey(out, pkey->pkey.dsa)) {
	    BIO_free(out);
	    ossl_raise(eDSAError, NULL);
	}
    }
    BIO_get_mem_ptr(out, &buf);
    str = rb_str_new(buf->data, buf->length);
    BIO_free(out);

    return str;
}

/*
 * Stores all parameters of key to the hash
 * INSECURE: PRIVATE INFORMATIONS CAN LEAK OUT!!!
 * Don't use :-)) (I's up to you)
 */
static VALUE
ossl_dsa_get_params(VALUE self)
{
    EVP_PKEY *pkey;
    VALUE hash;

    GetPKeyDSA(self, pkey);

    hash = rb_hash_new();

    rb_hash_aset(hash, rb_str_new2("p"), ossl_bn_new(pkey->pkey.dsa->p));
    rb_hash_aset(hash, rb_str_new2("q"), ossl_bn_new(pkey->pkey.dsa->q));
    rb_hash_aset(hash, rb_str_new2("g"), ossl_bn_new(pkey->pkey.dsa->g));
    rb_hash_aset(hash, rb_str_new2("pub_key"), ossl_bn_new(pkey->pkey.dsa->pub_key));
    rb_hash_aset(hash, rb_str_new2("priv_key"), ossl_bn_new(pkey->pkey.dsa->priv_key));
    
    return hash;
}

/*
 * Prints all parameters of key to buffer
 * INSECURE: PRIVATE INFORMATIONS CAN LEAK OUT!!!
 * Don't use :-)) (I's up to you)
 */
static VALUE
ossl_dsa_to_text(VALUE self)
{
    EVP_PKEY *pkey;
    BIO *out;
    BUF_MEM *buf;
    VALUE str;

    GetPKeyDSA(self, pkey);
    if (!(out = BIO_new(BIO_s_mem()))) {
	ossl_raise(eDSAError, NULL);
    }
    if (!DSA_print(out, pkey->pkey.dsa, 0)) { //offset = 0
	BIO_free(out);
	ossl_raise(eDSAError, NULL);
    }
    BIO_get_mem_ptr(out, &buf);
    str = rb_str_new(buf->data, buf->length);
    BIO_free(out);

    return str;
}

/*
 * Makes new instance DSA PUBLIC_KEY from PRIVATE_KEY
 */
static VALUE
ossl_dsa_to_public_key(VALUE self)
{
    EVP_PKEY *pkey;
    DSA *dsa;
    VALUE obj;
	
    GetPKeyDSA(self, pkey);
    /* err check performed by dsa_instance */
    dsa = DSAPublicKey_dup(pkey->pkey.dsa);
    obj = dsa_instance(CLASS_OF(self), dsa);
    if (obj == Qfalse) {
	DSA_free(dsa);
	ossl_raise(eDSAError, NULL);
    }
    return obj;
}

static VALUE
ossl_dsa_sign(VALUE self, VALUE data)
{
    EVP_PKEY *pkey;
    char *buf;
    int buf_len;
    VALUE str;

    GetPKeyDSA(self, pkey);
    StringValue(data);
    if (!DSA_PRIVATE(pkey->pkey.dsa)) {
	ossl_raise(eDSAError, "Private DSA key needed!");
    }
    if (!(buf = OPENSSL_malloc(DSA_size(pkey->pkey.dsa) + 16))) {
	ossl_raise(eDSAError, NULL);
    }
    if (!DSA_sign(0, RSTRING(data)->ptr, RSTRING(data)->len, buf,
		  &buf_len, pkey->pkey.dsa)) { /* type is ignored (0) */
	OPENSSL_free(buf);
	ossl_raise(eDSAError, NULL);
    }
    str = rb_str_new(buf, buf_len);
    OPENSSL_free(buf);

    return str;
}

static VALUE
ossl_dsa_verify(VALUE self, VALUE digest, VALUE sig)
{
    EVP_PKEY *pkey;
    int ret;

    GetPKeyDSA(self, pkey);
    StringValue(digest);
    StringValue(sig);
    /* type is ignored (0) */
    ret = DSA_verify(0, RSTRING(digest)->ptr, RSTRING(digest)->len,
		     RSTRING(sig)->ptr, RSTRING(sig)->len, pkey->pkey.dsa);
    if (ret < 0) {
	ossl_raise(eDSAError, NULL);
    }
    else if (ret == 1) {
	return Qtrue;
    }

    return Qfalse;
}

/*
 * INIT
 */
void
Init_ossl_dsa()
{
    eDSAError = rb_define_class_under(mPKey, "DSAError", ePKeyError);

    cDSA = rb_define_class_under(mPKey, "DSA", cPKey);
	
    rb_define_singleton_method(cDSA, "generate", ossl_dsa_s_generate, 1);
    rb_define_method(cDSA, "initialize", ossl_dsa_initialize, -1);

    rb_define_method(cDSA, "public?", ossl_dsa_is_public, 0);
    rb_define_method(cDSA, "private?", ossl_dsa_is_private, 0);
    rb_define_method(cDSA, "to_text", ossl_dsa_to_text, 0);
    rb_define_method(cDSA, "export", ossl_dsa_export, -1);
    rb_define_alias(cDSA, "to_pem", "export");
    rb_define_alias(cDSA, "to_s", "export");
    rb_define_method(cDSA, "public_key", ossl_dsa_to_public_key, 0);
    rb_define_method(cDSA, "syssign", ossl_dsa_sign, 1);
    rb_define_method(cDSA, "sysverify", ossl_dsa_verify, 2);

    rb_define_method(cDSA, "params", ossl_dsa_get_params, 0);
}

#else /* defined NO_DSA */
#   warning >>> OpenSSL is compiled without DSA support <<<

void
Init_ossl_dsa()
{
    rb_warning("OpenSSL is compiled without DSA support");
}

#endif /* NO_DSA */
