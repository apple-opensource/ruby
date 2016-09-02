/*
 * $Id: ossl_pkcs7.h,v 1.1 2003/07/23 16:11:29 gotoyuzo Exp $
 * 'OpenSSL for Ruby' project
 * Copyright (C) 2001-2002  Michal Rokos <m.rokos@sh.cvut.cz>
 * All rights reserved.
 */
/*
 * This program is licenced under the same licence as Ruby.
 * (See the file 'LICENCE'.)
 */
#if !defined(_OSSL_PKCS7_H_)
#define _OSSL_PKCS7_H_

extern VALUE mPKCS7;
extern VALUE cPKCS7;
extern VALUE cPKCS7SignerInfo;
extern VALUE ePKCS7Error;

void Init_ossl_pkcs7(void);

#endif /* _OSSL_PKCS7_H_ */

