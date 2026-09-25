// SPDX-License-Identifier: GPL-2.0
<<<<<<< HEAD
=======
#include <openssl/evp.h>
>>>>>>> origin/android16-base
#include <openssl/sha.h>
#include <openssl/md5.h>

int main(void)
{
<<<<<<< HEAD
	MD5_CTX context;
	unsigned char md[MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH];
	unsigned char dat[] = "12345";

	MD5_Init(&context);
	MD5_Update(&context, &dat[0], sizeof(dat));
	MD5_Final(&md[0], &context);
=======
	EVP_MD_CTX *mdctx;
	unsigned char md[MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH];
	unsigned char dat[] = "12345";
	unsigned int digest_len;

	mdctx = EVP_MD_CTX_new();
	if (!mdctx)
		return 0;

	EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);
	EVP_DigestUpdate(mdctx, &dat[0], sizeof(dat));
	EVP_DigestFinal_ex(mdctx, &md[0], &digest_len);
	EVP_MD_CTX_free(mdctx);
>>>>>>> origin/android16-base

	SHA1(&dat[0], sizeof(dat), &md[0]);

	return 0;
}
