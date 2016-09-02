/*
 * $Id: openssl_missing.h,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
 * 'OpenSSL for Ruby' project
 * Copyright (C) 2001-2002  Michal Rokos <m.rokos@sh.cvut.cz>
 * All rights reserved.
 */
/*
 * This program is licenced under the same licence as Ruby.
 * (See the file 'LICENCE'.)
 */
#if !defined(_OSSL_OPENSSL_MISSING_H_)
#define _OSSL_OPENSSL_MISSING_H_

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * These functions are not included in headers of OPENSSL <= 0.9.6b
 */

#if !defined(PEM_read_bio_DSAPublicKey)
# define PEM_read_bio_DSAPublicKey(bp,x,cb,u) (DSA *)PEM_ASN1_read_bio( \
        (char *(*)())d2i_DSAPublicKey,PEM_STRING_DSA_PUBLIC,bp,(char **)x,cb,u)
#endif

#if !defined(PEM_write_bio_DSAPublicKey)
# define PEM_write_bio_DSAPublicKey(bp,x) \
	PEM_ASN1_write_bio((int (*)())i2d_DSAPublicKey,\
		PEM_STRING_DSA_PUBLIC,\
		bp,(char *)x, NULL, NULL, 0, NULL, NULL)
#endif

#if !defined(DSAPrivateKey_dup)
# define DSAPrivateKey_dup(dsa) (DSA *)ASN1_dup((int (*)())i2d_DSAPrivateKey, \
	(char *(*)())d2i_DSAPrivateKey,(char *)dsa)
#endif

#if !defined(DSAPublicKey_dup)
# define DSAPublicKey_dup(dsa) (DSA *)ASN1_dup((int (*)())i2d_DSAPublicKey, \
	(char *(*)())d2i_DSAPublicKey,(char *)dsa)
#endif

#if !defined(X509_REVOKED_dup)
# define X509_REVOKED_dup(rev) (X509_REVOKED *)ASN1_dup((int (*)())i2d_X509_REVOKED, \
	(char *(*)())d2i_X509_REVOKED, (char *)rev)
#endif

#if !defined(PKCS7_SIGNER_INFO_dup)
#  define PKCS7_SIGNER_INFO_dup(si) (PKCS7_SIGNER_INFO *)ASN1_dup((int (*)())i2d_PKCS7_SIGNER_INFO, \
	(char *(*)())d2i_PKCS7_SIGNER_INFO, (char *)si)
#endif

#if !defined(PKCS7_RECIP_INFO_dup)
#  define PKCS7_RECIP_INFO_dup(ri) (PKCS7_RECIP_INFO *)ASN1_dup((int (*)())i2d_PKCS7_RECIP_INFO, \
	(char *(*)())d2i_PKCS7_RECIP_INFO, (char *)ri)
#endif

int HMAC_CTX_copy(HMAC_CTX *out, HMAC_CTX *in);
void *X509_STORE_get_ex_data(X509_STORE *str, int idx);
int X509_STORE_set_ex_data(X509_STORE *str, int idx, void *data);
EVP_MD_CTX *EVP_MD_CTX_create(void);
int EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx);
void EVP_MD_CTX_destroy(EVP_MD_CTX *ctx);

#if !defined(EVP_CIPHER_name)
#  define EVP_CIPHER_name(e) OBJ_nid2sn(EVP_CIPHER_nid(e))
#endif

#if !defined(EVP_MD_name)
#  define EVP_MD_name(e) OBJ_nid2sn(EVP_MD_type(e))
#endif

void EVP_MD_CTX_init(EVP_MD_CTX *ctx);
void HMAC_CTX_init(HMAC_CTX *ctx);
void HMAC_CTX_cleanup(HMAC_CTX *ctx);

#if !defined(PKCS7_is_detached)
#  define PKCS7_is_detached(p7) (PKCS7_type_is_signed(p7) && PKCS7_get_detached(p7))
#endif

#if !defined(PKCS7_type_is_encrypted)
#  define PKCS7_type_is_encrypted(a) (OBJ_obj2nid((a)->type) == NID_pkcs7_encrypted)
#endif

int X509_CRL_set_version(X509_CRL *x, long version);
int X509_CRL_set_issuer_name(X509_CRL *x, X509_NAME *name);
int X509_CRL_sort(X509_CRL *c);
int X509_CRL_add0_revoked(X509_CRL *crl, X509_REVOKED *rev);
int BN_mod_sqr(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
char *CONF_get1_default_config_file(void);

#if !defined(HAVE_PEM_DEF_CALLBACK)
int PEM_def_callback(char *buf, int num, int w, void *key);
#endif

#if defined(__cplusplus)
}
#endif


#endif /* _OSSL_OPENSSL_MISSING_H_ */

