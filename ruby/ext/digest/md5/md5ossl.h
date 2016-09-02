/* $Id: md5ossl.h,v 1.1.1.1 2003/10/15 10:11:47 melville Exp $ */

#ifndef MD5OSSL_H_INCLUDED
#define MD5OSSL_H_INCLUDED

#include <openssl/md5.h>

void MD5_End(MD5_CTX *pctx, unsigned char *hexdigest);
int MD5_Equal(MD5_CTX *pctx1, MD5_CTX *pctx2);

#endif
