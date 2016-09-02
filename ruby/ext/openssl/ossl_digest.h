/*
 * $Id: ossl_digest.h,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $
 * 'OpenSSL for Ruby' project
 * Copyright (C) 2001-2002  Michal Rokos <m.rokos@sh.cvut.cz>
 * All rights reserved.
 */
/*
 * This program is licenced under the same licence as Ruby.
 * (See the file 'LICENCE'.)
 */
#if !defined(_OSSL_DIGEST_H_)
#define _OSSL_DIGEST_H_

extern VALUE mDigest;
extern VALUE cDigest;
extern VALUE eDigestError;

const EVP_MD *GetDigestPtr(VALUE);
void Init_ossl_digest(void);

#endif /* _OSSL_DIGEST_H_ */

