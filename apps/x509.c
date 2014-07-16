/* apps/x509.c */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef OPENSSL_NO_STDIO
#define APPS_WIN16
#endif
#include "apps.h"
#include <openssl/bio.h>
#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif
#ifndef OPENSSL_NO_DSA
#include <openssl/dsa.h>
#endif

#undef POSTFIX
#define	POSTFIX	".srl"
#define DEF_DAYS	30

static int callb(int ok, X509_STORE_CTX *ctx);
static int sign (X509 *x, EVP_PKEY *pkey,int days,int clrext, const EVP_MD *digest,
						CONF *conf, char *section);
static int x509_certify (X509_STORE *ctx,char *CAfile,const EVP_MD *digest,
			 X509 *x,X509 *xca,EVP_PKEY *pkey,
			 STACK_OF(OPENSSL_STRING) *sigopts,
			 char *serial, int create ,int days, int clrext,
			 CONF *conf, char *section, ASN1_INTEGER *sno,
			 int reqfile);
static int purpose_print(BIO *bio, X509 *cert, X509_PURPOSE *pt);

#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
static int force_version=2;
#endif

enum options {
	OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
	OPT_INFORM, OPT_OUTFORM, OPT_KEYFORM, OPT_REQ, OPT_CAFORM,
	OPT_CAKEYFORM, OPT_SIGOPT, OPT_DAYS, OPT_PASSIN, OPT_EXTFILE,
	OPT_EXTENSIONS, OPT_IN, OPT_OUT, OPT_SIGNKEY, OPT_CA,
	OPT_CAKEY, OPT_CASERIAL, OPT_SET_SERIAL, OPT_FORCE_PUBKEY,
	OPT_ADDTRUST, OPT_ADDREJECT, OPT_SETALIAS, OPT_CERTOPT, OPT_NAMEOPT,
	OPT_C, OPT_EMAIL, OPT_OCSP_URI, OPT_SERIAL, OPT_NEXT_SERIAL,
	OPT_MODULUS, OPT_PUBKEY, OPT_X509TOREQ, OPT_TEXT, OPT_HASH,
	OPT_ISSUER_HASH, OPT_SUBJECT, OPT_ISSUER, OPT_FINGERPRINT, OPT_DATES,
	OPT_PURPOSE, OPT_STARTDATE, OPT_ENDDATE, OPT_CHECKEND, OPT_CHECKHOST,
	OPT_CHECKEMAIL, OPT_CHECKIP, OPT_NOOUT, OPT_TRUSTOUT, OPT_CLRTRUST,
	OPT_CLRREJECT, OPT_ALIAS, OPT_CACREATESERIAL, OPT_CLREXT, OPT_OCSPID,
	OPT_BADSIG, OPT_MD, OPT_ENGINE,
#ifndef OPENSSL_NO_MD5
	OPT_SUBJECT_HASH_OLD,
	OPT_ISSUER_HASH_OLD,
#endif
#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
	OPT_FORCE_VERSION,
#endif
};

OPTIONS x509_options[] = {
	{ "help", OPT_HELP, '-', "Display this summary" },
	{ "inform", OPT_INFORM, 'f', "Input format - default PEM (one of DER, NET or PEM)" },
	{ "in", OPT_IN, '<', "Input file - default stdin" },
	{" outform", OPT_OUTFORM, 'f', "Output format - default PEM (one of DER, NET or PEM)" },
	{ "out", OPT_OUT, '>', "Output file - default stdout" },
	{ "keyform", OPT_KEYFORM, 'F', "Private key format - default PEM" },
	{ "passin", OPT_PASSIN, 's', "Private key password source" },
	{ "serial", OPT_SERIAL, '-', "Print serial number value" },
	{ "subject_hash", OPT_HASH, '-', "Print subject hash value" },
	{ "issuer_hash", OPT_ISSUER_HASH, '-', "Print issuer hash value" },
#ifndef OPENSSL_NO_MD5
	{ "subject_hash_old", OPT_SUBJECT_HASH_OLD, '-', "Print old-style (MD5) issuer hash value" },
	{ "issuer_hash_old", OPT_ISSUER_HASH_OLD, '-', "Print old-style (MD5) subject hash value" },
#endif
	{ "hash", OPT_HASH, '-', "Synonym for -subject_hash" },
	{ "subject", OPT_SUBJECT, '-', "Print subject DN" },
	{ "issuer", OPT_ISSUER, '-', "Print issuer DN" },
	{ "email", OPT_EMAIL, '-', "Print email address(es)" },
	{ "startdate", OPT_STARTDATE, '-', "Set notBefore field" },
	{ "enddate", OPT_ENDDATE, '-', "Set notAfter field" },
	{ "purpose", OPT_PURPOSE, '-', "Print out certificate purposes" },
	{ "dates", OPT_DATES, '-', "Both Before and After dates" },
	{ "modulus", OPT_MODULUS, '-', "Print the RSA key modulus" },
	{ "pubkey", OPT_PUBKEY, '-', "Output the public key" },
	{ "fingerprint", OPT_FINGERPRINT, '-', "Print the certificate fingerprint" },
	{ "alias", OPT_ALIAS, '-', "Output certificate alias" },
	{ "noout", OPT_NOOUT, '-', "No certificate output" },
	{ "ocspid", OPT_OCSPID, '-', "Print OCSP hash values for the subject name and public key" },
	{ "ocsp_uri", OPT_OCSP_URI, '-', "Print OCSP Responder URL(s)" },
	{ "trustout", OPT_TRUSTOUT, '-', "Output a trusted certificate" },
	{ "clrtrust", OPT_CLRTRUST, '-', "Clear all trusted purposes" },
	{ "clrext", OPT_CLREXT, '-', "Clear all rejected purposes" },
	{ "addtrust", OPT_ADDTRUST, 's', "Trust certificate for a given purpose" },
	{ "addreject", OPT_ADDREJECT, 's', "Reject certificate for a given purpose" },
	{ "setalias", OPT_SETALIAS, 's', "Set certificate alias" },
	{ "days", OPT_DAYS, 'p', "How long till expiry of a signed certificate - def 30 days" },
	{ "checkend", OPT_CHECKEND, 'p', "Check whether the cert expires in the next arg seconds" },
	{ OPT_MORE_STR, 1, 1, "Exit 1 if so, 0 if not" },
	{ "signkey", OPT_SIGNKEY, '<', "Self sign cert with arg" },
	{ "x509toreq", OPT_X509TOREQ, '-', "Output a certification request object" },
	{ "req", OPT_REQ, '-', "Input is a certificate request, sign and output" },
	{ "CA", OPT_CA, '<', "Set the CA certificate, must be PEM format" },
	{ "CAkey", OPT_CAKEY, '<', "The CA key, must be PEM format; if not in CAfile" },
	{ "CAcreateserial", OPT_CACREATESERIAL, '-', "Create serial number file if it does not exist" },
	{ "CAserial", OPT_CASERIAL, '<', "Serial file" },
	{ "set_serial", OPT_SET_SERIAL, 's', "Serial number to use" },
	{ "text", OPT_TEXT, '-', "Print the certificate in text form" },
	{ "C", OPT_C, '-', "Print out C code forms" },
	{ "extfile", OPT_EXTFILE, '<', "File with X509V3 extensions to add" },
	{ "extensions", OPT_EXTENSIONS, 's', "Section from config file to use" },
	{ "nameopt", OPT_NAMEOPT, 's', "Various certificate name options" },
	{ "certopt", OPT_CERTOPT, 's', "Various certificate text options" },
	{ "checkhost", OPT_CHECKHOST, 's', "Check certificate matches host" },
	{ "checkemail", OPT_CHECKEMAIL, 's', "Check certificate matches email" },
	{ "checkip", OPT_CHECKIP, 's', "Check certificate matches ipaddr" },
	{ "CAform", OPT_CAFORM, 'F', "CA format - default PEM" },
	{ "CAkeyform", OPT_CAKEYFORM, 'F', "CA key format - default PEM" },
	{ "sigopt", OPT_SIGOPT, 's' },
	{ "force_pubkey", OPT_FORCE_PUBKEY, '<' },
	{ "next_serial", OPT_NEXT_SERIAL, '-' },
	{ "clrreject", OPT_CLRREJECT, '-' },
	{ "badsig", OPT_BADSIG, '-' },
	{ "", OPT_MD, '-', "Any supported digest algoritm" },
#ifndef OPENSSL_NO_ENGINE
	{ "engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device" },
#endif
#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
	{ "force_version", OPT_FORCE_VERSION, 'p' },
#endif
	{ NULL }
};

int x509_main(int argc, char **argv)
	{
	ASN1_INTEGER *sno=NULL;
	ASN1_OBJECT *objtmp;
	BIO *out=NULL;
	CONF *extconf=NULL;
	EVP_PKEY *Upkey=NULL, *CApkey=NULL, *fkey=NULL;
	STACK_OF(ASN1_OBJECT) *trust=NULL, *reject=NULL;
	STACK_OF(OPENSSL_STRING) *sigopts=NULL;
	X509 *x=NULL, *xca=NULL;
	X509_REQ *req=NULL, *rq=NULL;
	X509_STORE *ctx=NULL;
	const EVP_MD *digest=NULL;
	char *CAkeyfile=NULL, *CAserial=NULL, *fkeyfile=NULL, *alias=NULL;
	char *checkhost=NULL, *checkemail=NULL, *checkip=NULL;
	char *extsect=NULL, *extfile=NULL, *passin=NULL, *passinarg=NULL;
	char *infile=NULL, *outfile=NULL, *keyfile=NULL, *CAfile=NULL;
	char buf[256];
	char *engine=NULL, *prog;
	int C=0, x509req=0, days=DEF_DAYS, modulus=0, pubkey=0, pprint=0;
	int CAformat=FORMAT_PEM, CAkeyformat=FORMAT_PEM;
	int fingerprint=0, reqfile=0, need_rand=0, checkend=0, checkoffset=0;
	int informat=FORMAT_PEM, outformat=FORMAT_PEM, keyformat=FORMAT_PEM;
	int next_serial=0, subject_hash=0, issuer_hash=0, ocspid=0;
	int noout=0, sign_flag=0, CA_flag=0, CA_createserial=0, email=0;
	int ocsp_uri=0, trustout=0, clrtrust=0, clrreject=0, aliasout=0;
	int ret=1, i, num, badsig=0, clrext=0;
	int text=0, serial=0, subject=0, issuer=0, startdate=0, enddate=0;
	unsigned long nmflag=0, certflag=0;
	enum options o;
#ifndef OPENSSL_NO_ENGINE
	ENGINE *e=NULL;
#endif
#ifndef OPENSSL_NO_MD5
	int subject_hash_old=0, issuer_hash_old=0;
#endif

	ctx=X509_STORE_new();
	if (ctx == NULL)
		goto end;
	X509_STORE_set_verify_cb(ctx,callb);

	prog = opt_init(argc, argv, x509_options);
	while ((o = opt_next()) != OPT_EOF) {
		switch (o) {
		case OPT_EOF:
		case OPT_ERR:
			BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
			goto end;
		case OPT_HELP:
err:
			opt_help(x509_options);
			goto end;
		case OPT_INFORM:
			opt_format(opt_arg(), 1, &informat);
			break;
		case OPT_IN:
			infile = opt_arg();
			break;
		case OPT_OUTFORM:
			opt_format(opt_arg(), 1, &outformat);
			break;
		case OPT_KEYFORM:
			opt_format(opt_arg(), 1, &keyformat);
			break;
		case OPT_CAFORM:
			opt_format(opt_arg(), 1, &CAformat);
			break;
		case OPT_CAKEYFORM:
			opt_format(opt_arg(), 1, &CAkeyformat);
			break;
		case OPT_OUT:
			outfile = opt_arg();
			break;
		case OPT_REQ:
			reqfile = need_rand = 1;
			break;

		case OPT_SIGOPT:
			if (!sigopts)
				sigopts = sk_OPENSSL_STRING_new_null();
			if (!sigopts || !sk_OPENSSL_STRING_push(sigopts, opt_arg()))
				goto err;
			break;
#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
		case OPT_FORCE_VERSION:
			force_version=atoi(opt_arg()) - 1;
			break;
#endif
		case OPT_DAYS:
			days=atoi(opt_arg());
			break;
		case OPT_PASSIN:
			passinarg = opt_arg();
			break;
		case OPT_EXTFILE:
			extfile = opt_arg();
			break;
		case OPT_EXTENSIONS:
			extsect = opt_arg();
			break;
		case OPT_SIGNKEY:
			keyfile = opt_arg();
			sign_flag= ++num;
			need_rand = 1;
			break;
		case OPT_CA:
			CAfile = opt_arg();
			CA_flag= ++num;
			need_rand = 1;
			break;
		case OPT_CAKEY:
			CAkeyfile = opt_arg();
			break;
		case OPT_CASERIAL:
			CAserial = opt_arg();
			break;
		case OPT_SET_SERIAL:
			if ((sno = s2i_ASN1_INTEGER(NULL, opt_arg())) == NULL)
				goto err;
			break;
		case OPT_FORCE_PUBKEY:
			fkeyfile = opt_arg();
			break;
		case OPT_ADDTRUST:
			if ((objtmp = OBJ_txt2obj(opt_arg(), 0)) == NULL) {
				BIO_printf(bio_err,
					"%s: Invalid trust object value %s\n",
					prog, opt_arg());
				goto err;
			}
			if (trust == NULL)
				trust = sk_ASN1_OBJECT_new_null();
			sk_ASN1_OBJECT_push(trust, objtmp);
			trustout = 1;
			break;
		case OPT_ADDREJECT:
			if ((objtmp = OBJ_txt2obj(opt_arg(), 0)) == NULL) {
				BIO_printf(bio_err,
					"%s: Invalid reject object value %s\n",
					prog, opt_arg());
				goto err;
			}
			if (reject == NULL)
				reject = sk_ASN1_OBJECT_new_null();
			sk_ASN1_OBJECT_push(reject, objtmp);
			trustout = 1;
			break;
		case OPT_SETALIAS:
			alias = opt_arg();
			trustout = 1;
			break;
		case OPT_CERTOPT:
			if (!set_cert_ex(&certflag, opt_arg()))
				goto err;
			break;
		case OPT_NAMEOPT:
			if (!set_name_ex(&nmflag, opt_arg()))
				goto err;
			break;
		case OPT_ENGINE:
			engine = opt_arg();
			break;
		case OPT_C:
			C= ++num;
			break;
		case OPT_EMAIL:
			email= ++num;
			break;
		case OPT_OCSP_URI:
			ocsp_uri= ++num;
			break;
		case OPT_SERIAL:
			serial= ++num;
			break;
		case OPT_NEXT_SERIAL:
			next_serial= ++num;
			break;
		case OPT_MODULUS:
			modulus= ++num;
			break;
		case OPT_PUBKEY:
			pubkey= ++num;
			break;
		case OPT_X509TOREQ:
			x509req= ++num;
			break;
		case OPT_TEXT:
			text= ++num;
			break;
		case OPT_SUBJECT:
			subject= ++num;
			break;
		case OPT_ISSUER:
			issuer= ++num;
			break;
		case OPT_FINGERPRINT:
			fingerprint= ++num;
			break;
		case OPT_HASH:
			subject_hash= ++num;
			break;
		case OPT_ISSUER_HASH:
			issuer_hash= ++num;
			break;
		case OPT_PURPOSE:
			pprint= ++num;
			break;
		case OPT_STARTDATE:
			startdate= ++num;
			break;
		case OPT_ENDDATE:
			enddate= ++num;
			break;
		case OPT_NOOUT:
			noout= ++num;
			break;
		case OPT_TRUSTOUT:
			trustout= 1;
			break;
		case OPT_CLRTRUST:
			clrtrust= ++num;
			break;
		case OPT_CLRREJECT:
			clrreject= ++num;
			break;
		case OPT_ALIAS:
			aliasout= ++num;
			break;
		case OPT_CACREATESERIAL:
			CA_createserial= ++num;
			break;
#if 0
		case OPT_CRLEXT:
			/* stay backwards-compatible with 0.9.5; this should go away soon */
			BIO_printf(bio_err,
				"%s:  Use -clrext instead of -crlext\n",
				prog);
#endif
		case OPT_CLREXT:
			clrext = 1;
			break;
		case OPT_OCSPID:
			ocspid= ++num;
			break;
		case OPT_BADSIG:
			badsig = 1;
			break;
#ifndef OPENSSL_NO_MD5
		case OPT_SUBJECT_HASH_OLD:
			subject_hash_old= ++num;
			break;
		case OPT_ISSUER_HASH_OLD:
			issuer_hash_old= ++num;
			break;
#endif
		case OPT_DATES:
			startdate= ++num;
			enddate= ++num;
			break;
		case OPT_CHECKEND:
			checkoffset=atoi(opt_arg());
			checkend=1;
			break;
		case OPT_CHECKHOST:
			checkhost = opt_arg();
			break;
		case OPT_CHECKEMAIL:
			checkemail = opt_arg();
			break;
		case OPT_CHECKIP:
			checkip = opt_arg();
			break;
		case OPT_MD:
			if (!opt_md(opt_arg(), &digest))
				goto err;
		}
	}

#ifndef OPENSSL_NO_ENGINE
        e = setup_engine(bio_err, engine, 0);
#endif

	if (need_rand)
		app_RAND_load_file(NULL, bio_err, 0);

	if (!app_passwd(bio_err, passinarg, NULL, &passin, NULL))
		{
		BIO_printf(bio_err, "Error getting password\n");
		goto end;
		}

	if (!X509_STORE_set_default_paths(ctx))
		{
		ERR_print_errors(bio_err);
		goto end;
		}

	if (fkeyfile)
		{
		fkey = load_pubkey(bio_err, fkeyfile, keyformat, 0,
						NULL, e, "Forced key");
		if (fkey == NULL) goto end;
		}

	if ((CAkeyfile == NULL) && (CA_flag) && (CAformat == FORMAT_PEM))
		{ CAkeyfile=CAfile; }
	else if ((CA_flag) && (CAkeyfile == NULL))
		{
		BIO_printf(bio_err,"need to specify a CAkey if using the CA command\n");
		goto end;
		}

	if (extfile)
		{
		long errorline = -1;
		X509V3_CTX ctx2;
		extconf = NCONF_new(NULL);
		if (!NCONF_load(extconf, extfile,&errorline))
			{
			if (errorline <= 0)
				BIO_printf(bio_err,
					"error loading the config file '%s'\n",
								extfile);
                	else
                        	BIO_printf(bio_err,
				       "error on line %ld of config file '%s'\n"
							,errorline,extfile);
			goto end;
			}
		if (!extsect)
			{
			extsect = NCONF_get_string(extconf, "default", "extensions");
			if (!extsect)
				{
				ERR_clear_error();
				extsect = "default";
				}
			}
		X509V3_set_ctx_test(&ctx2);
		X509V3_set_nconf(&ctx2, extconf);
		if (!X509V3_EXT_add_nconf(extconf, &ctx2, extsect, NULL))
			{
			BIO_printf(bio_err,
				"Error Loading extension section %s\n",
								 extsect);
			ERR_print_errors(bio_err);
			goto end;
			}
		}


	if (reqfile)
		{
		EVP_PKEY *pkey;
		BIO *in;

		if (!sign_flag && !CA_flag)
			{
			BIO_printf(bio_err,"We need a private key to sign with\n");
			goto end;
			}
		in = bio_open_default(infile, "r");
		if (in == NULL)
			goto end;
		req=PEM_read_bio_X509_REQ(in,NULL,NULL,NULL);
		BIO_free(in);

		if (req == NULL)
			{
			ERR_print_errors(bio_err);
			goto end;
			}

		if (	(req->req_info == NULL) ||
			(req->req_info->pubkey == NULL) ||
			(req->req_info->pubkey->public_key == NULL) ||
			(req->req_info->pubkey->public_key->data == NULL))
			{
			BIO_printf(bio_err,"The certificate request appears to corrupted\n");
			BIO_printf(bio_err,"It does not contain a public key\n");
			goto end;
			}
		if ((pkey=X509_REQ_get_pubkey(req)) == NULL)
	                {
	                BIO_printf(bio_err,"error unpacking public key\n");
	                goto end;
	                }
		i=X509_REQ_verify(req,pkey);
		EVP_PKEY_free(pkey);
		if (i < 0)
			{
			BIO_printf(bio_err,"Signature verification error\n");
			ERR_print_errors(bio_err);
			goto end;
			}
	        if (i == 0)
			{
			BIO_printf(bio_err,"Signature did not match the certificate request\n");
			goto end;
			}
		else
			BIO_printf(bio_err,"Signature ok\n");

		print_name(bio_err, "subject=", X509_REQ_get_subject_name(req), nmflag);

		if ((x=X509_new()) == NULL) goto end;

		if (sno == NULL)
			{
			sno = ASN1_INTEGER_new();
			if (!sno || !rand_serial(NULL, sno))
				goto end;
			if (!X509_set_serialNumber(x, sno)) 
				goto end;
			ASN1_INTEGER_free(sno);
			sno = NULL;
			}
		else if (!X509_set_serialNumber(x, sno)) 
			goto end;

		if (!X509_set_issuer_name(x,req->req_info->subject)) goto end;
		if (!X509_set_subject_name(x,req->req_info->subject)) goto end;

		X509_gmtime_adj(X509_get_notBefore(x),0);
	        X509_time_adj_ex(X509_get_notAfter(x),days, 0, NULL);
		if (fkey)
			X509_set_pubkey(x, fkey);
		else
			{
			pkey = X509_REQ_get_pubkey(req);
			X509_set_pubkey(x,pkey);
			EVP_PKEY_free(pkey);
			}
		}
	else
		x=load_cert(bio_err,infile,informat,NULL,e,"Certificate");

	if (x == NULL) goto end;
	if (CA_flag)
		{
		xca=load_cert(bio_err,CAfile,CAformat,NULL,e,"CA Certificate");
		if (xca == NULL) goto end;
		}

	if (!noout || text || next_serial)
		{
		OBJ_create("2.99999.3",
			"SET.ex3","SET x509v3 extension 3");

		out = bio_open_default(outfile, "w");
		if (out == NULL)
			goto end;
		}

	if (alias) X509_alias_set1(x, (unsigned char *)alias, -1);

	if (clrtrust) X509_trust_clear(x);
	if (clrreject) X509_reject_clear(x);

	if (trust)
		{
		for (i = 0; i < sk_ASN1_OBJECT_num(trust); i++)
			{
			objtmp = sk_ASN1_OBJECT_value(trust, i);
			X509_add1_trust_object(x, objtmp);
			}
		}

	if (reject)
		{
		for (i = 0; i < sk_ASN1_OBJECT_num(reject); i++)
			{
			objtmp = sk_ASN1_OBJECT_value(reject, i);
			X509_add1_reject_object(x, objtmp);
			}
		}

	if (num)
		{
		for (i=1; i<=num; i++)
			{
			if (issuer == i)
				{
				print_name(bio_out, "issuer= ",
					X509_get_issuer_name(x), nmflag);
				}
			else if (subject == i) 
				{
				print_name(bio_out, "subject= ",
					X509_get_subject_name(x), nmflag);
				}
			else if (serial == i)
				{
				BIO_printf(bio_out,"serial=");
				i2a_ASN1_INTEGER(bio_out,
					X509_get_serialNumber(x));
				BIO_printf(bio_out,"\n");
				}
			else if (next_serial == i)
				{
				BIGNUM *bnser;
				ASN1_INTEGER *ser;
				ser = X509_get_serialNumber(x);
				bnser = ASN1_INTEGER_to_BN(ser, NULL);
				if (!bnser)
					goto end;
				if (!BN_add_word(bnser, 1))
					goto end;
				ser = BN_to_ASN1_INTEGER(bnser, NULL);
				if (!ser)
					goto end;
				BN_free(bnser);
				i2a_ASN1_INTEGER(out, ser);
				ASN1_INTEGER_free(ser);
				BIO_puts(out, "\n");
				}
			else if ((email == i) || (ocsp_uri == i))
				{
				int j;
				STACK_OF(OPENSSL_STRING) *emlst;
				if (email == i)
					emlst = X509_get1_email(x);
				else
					emlst = X509_get1_ocsp(x);
				for (j = 0; j < sk_OPENSSL_STRING_num(emlst); j++)
					BIO_printf(bio_out, "%s\n",
						   sk_OPENSSL_STRING_value(emlst, j));
				X509_email_free(emlst);
				}
			else if (aliasout == i)
				{
				unsigned char *alstr;
				alstr = X509_alias_get0(x, NULL);
				if (alstr) BIO_printf(bio_out,"%s\n", alstr);
				else BIO_puts(bio_out,"<No Alias>\n");
				}
			else if (subject_hash == i)
				{
				BIO_printf(bio_out,"%08lx\n",X509_subject_name_hash(x));
				}
#ifndef OPENSSL_NO_MD5
			else if (subject_hash_old == i)
				{
				BIO_printf(bio_out,"%08lx\n",X509_subject_name_hash_old(x));
				}
#endif
			else if (issuer_hash == i)
				{
				BIO_printf(bio_out,"%08lx\n",X509_issuer_name_hash(x));
				}
#ifndef OPENSSL_NO_MD5
			else if (issuer_hash_old == i)
				{
				BIO_printf(bio_out,"%08lx\n",X509_issuer_name_hash_old(x));
				}
#endif
			else if (pprint == i)
				{
				X509_PURPOSE *ptmp;
				int j;
				BIO_printf(bio_out, "Certificate purposes:\n");
				for (j = 0; j < X509_PURPOSE_get_count(); j++)
					{
					ptmp = X509_PURPOSE_get0(j);
					purpose_print(bio_out, x, ptmp);
					}
				}
			else
				if (modulus == i)
				{
				EVP_PKEY *pkey;

				pkey=X509_get_pubkey(x);
				if (pkey == NULL)
					{
					BIO_printf(bio_err,"Modulus=unavailable\n");
					ERR_print_errors(bio_err);
					goto end;
					}
				BIO_printf(bio_out,"Modulus=");
#ifndef OPENSSL_NO_RSA
				if (pkey->type == EVP_PKEY_RSA)
					BN_print(bio_out,pkey->pkey.rsa->n);
				else
#endif
#ifndef OPENSSL_NO_DSA
				if (pkey->type == EVP_PKEY_DSA)
					BN_print(bio_out,pkey->pkey.dsa->pub_key);
				else
#endif
					BIO_printf(bio_out,"Wrong Algorithm type");
				BIO_printf(bio_out,"\n");
				EVP_PKEY_free(pkey);
				}
			else
				if (pubkey == i)
				{
				EVP_PKEY *pkey;

				pkey=X509_get_pubkey(x);
				if (pkey == NULL)
					{
					BIO_printf(bio_err,"Error getting public key\n");
					ERR_print_errors(bio_err);
					goto end;
					}
				PEM_write_bio_PUBKEY(bio_out, pkey);
				EVP_PKEY_free(pkey);
				}
			else
				if (C == i)
				{
				unsigned char *d;
				char *m;
				int y,z;

				X509_NAME_oneline(X509_get_subject_name(x),
					buf,sizeof buf);
				BIO_printf(bio_out,"/* subject:%s */\n",buf);
				m=X509_NAME_oneline(
					X509_get_issuer_name(x),buf,
					sizeof buf);
				BIO_printf(bio_out,"/* issuer :%s */\n",buf);

				z=i2d_X509(x,NULL);
				m=OPENSSL_malloc(z);

				d=(unsigned char *)m;
				z=i2d_X509_NAME(X509_get_subject_name(x),&d);
				BIO_printf(bio_out,"unsigned char XXX_subject_name[%d]={\n",z);
				d=(unsigned char *)m;
				for (y=0; y<z; y++)
					{
					BIO_printf(bio_out,"0x%02X,",d[y]);
					if ((y & 0x0f) == 0x0f) BIO_printf(bio_out,"\n");
					}
				if (y%16 != 0) BIO_printf(bio_out,"\n");
				BIO_printf(bio_out,"};\n");

				z=i2d_X509_PUBKEY(X509_get_X509_PUBKEY(x),&d);
				BIO_printf(bio_out,"unsigned char XXX_public_key[%d]={\n",z);
				d=(unsigned char *)m;
				for (y=0; y<z; y++)
					{
					BIO_printf(bio_out,"0x%02X,",d[y]);
					if ((y & 0x0f) == 0x0f)
						BIO_printf(bio_out,"\n");
					}
				if (y%16 != 0) BIO_printf(bio_out,"\n");
				BIO_printf(bio_out,"};\n");

				z=i2d_X509(x,&d);
				BIO_printf(bio_out,"unsigned char XXX_certificate[%d]={\n",z);
				d=(unsigned char *)m;
				for (y=0; y<z; y++)
					{
					BIO_printf(bio_out,"0x%02X,",d[y]);
					if ((y & 0x0f) == 0x0f)
						BIO_printf(bio_out,"\n");
					}
				if (y%16 != 0) BIO_printf(bio_out,"\n");
				BIO_printf(bio_out,"};\n");

				OPENSSL_free(m);
				}
			else if (text == i)
				{
				X509_print_ex(bio_out,x,nmflag, certflag);
				}
			else if (startdate == i)
				{
				BIO_puts(bio_out,"notBefore=");
				ASN1_TIME_print(bio_out,X509_get_notBefore(x));
				BIO_puts(bio_out,"\n");
				}
			else if (enddate == i)
				{
				BIO_puts(bio_out,"notAfter=");
				ASN1_TIME_print(bio_out,X509_get_notAfter(x));
				BIO_puts(bio_out,"\n");
				}
			else if (fingerprint == i)
				{
				int j;
				unsigned int n;
				unsigned char md[EVP_MAX_MD_SIZE];
				const EVP_MD *fdig = digest;

				if (!fdig)
					fdig = EVP_sha1();

				if (!X509_digest(x,fdig,md,&n))
					{
					BIO_printf(bio_err,"out of memory\n");
					goto end;
					}
				BIO_printf(bio_out,"%s Fingerprint=",
						OBJ_nid2sn(EVP_MD_type(fdig)));
				for (j=0; j<(int)n; j++)
					{
					BIO_printf(bio_out,"%02X%c",md[j],
						(j+1 == (int)n)
						?'\n':':');
					}
				}

			/* should be in the library */
			else if ((sign_flag == i) && (x509req == 0))
				{
				BIO_printf(bio_err,"Getting Private key\n");
				if (Upkey == NULL)
					{
					Upkey=load_key(bio_err,
						keyfile, keyformat, 0,
						passin, e, "Private key");
					if (Upkey == NULL) goto end;
					}

				assert(need_rand);
				if (!sign(x,Upkey,days,clrext,digest,
						 extconf, extsect)) goto end;
				}
			else if (CA_flag == i)
				{
				BIO_printf(bio_err,"Getting CA Private Key\n");
				if (CAkeyfile != NULL)
					{
					CApkey=load_key(bio_err,
						CAkeyfile, CAkeyformat,
						0, passin, e,
						"CA Private Key");
					if (CApkey == NULL) goto end;
					}
				
				assert(need_rand);
				if (!x509_certify(ctx,CAfile,digest,x,xca,
					CApkey, sigopts,
					CAserial,CA_createserial,days, clrext,
					extconf, extsect, sno, reqfile))
					goto end;
				}
			else if (x509req == i)
				{
				EVP_PKEY *pk;

				BIO_printf(bio_err,"Getting request Private Key\n");
				if (keyfile == NULL)
					{
					BIO_printf(bio_err,"no request key file specified\n");
					goto end;
					}
				else
					{
					pk=load_key(bio_err,
						keyfile, keyformat, 0,
						passin, e, "request key");
					if (pk == NULL) goto end;
					}

				BIO_printf(bio_err,"Generating certificate request\n");

				rq=X509_to_X509_REQ(x,pk,digest);
				EVP_PKEY_free(pk);
				if (rq == NULL)
					{
					ERR_print_errors(bio_err);
					goto end;
					}
				if (!noout)
					{
					X509_REQ_print(out,rq);
					PEM_write_bio_X509_REQ(out,rq);
					}
				noout=1;
				}
			else if (ocspid == i)
				{
				X509_ocspid_print(out, x);
				}
			}
		}

	if (checkend)
		{
		time_t tcheck=time(NULL) + checkoffset;

		if (X509_cmp_time(X509_get_notAfter(x), &tcheck) < 0)
			{
			BIO_printf(out,"Certificate will expire\n");
			ret=1;
			}
		else
			{
			BIO_printf(out,"Certificate will not expire\n");
			ret=0;
			}
		goto end;
		}

	print_cert_checks(bio_out, x, checkhost, checkemail, checkip);

	if (noout)
		{
		ret=0;
		goto end;
		}

	if (badsig)
		x->signature->data[x->signature->length - 1] ^= 0x1;

	if 	(outformat == FORMAT_ASN1)
		i=i2d_X509_bio(out,x);
	else if (outformat == FORMAT_PEM)
		{
		if (trustout) i=PEM_write_bio_X509_AUX(out,x);
		else i=PEM_write_bio_X509(out,x);
		}
	else if (outformat == FORMAT_NETSCAPE)
		{
		NETSCAPE_X509 nx;
		ASN1_OCTET_STRING hdr;

		hdr.data=(unsigned char *)NETSCAPE_CERT_HDR;
		hdr.length=strlen(NETSCAPE_CERT_HDR);
		nx.header= &hdr;
		nx.cert=x;

		i=ASN1_item_i2d_bio(ASN1_ITEM_rptr(NETSCAPE_X509),out,&nx);
		}
	else	{
		BIO_printf(bio_err,"bad output format specified for outfile\n");
		goto end;
		}
	if (!i)
		{
		BIO_printf(bio_err,"unable to write certificate\n");
		ERR_print_errors(bio_err);
		goto end;
		}
	ret=0;
end:
	if (need_rand)
		app_RAND_write_file(NULL, bio_err);
	OBJ_cleanup();
	NCONF_free(extconf);
	BIO_free_all(out);
	X509_STORE_free(ctx);
	X509_REQ_free(req);
	X509_free(x);
	X509_free(xca);
	EVP_PKEY_free(Upkey);
	EVP_PKEY_free(CApkey);
	EVP_PKEY_free(fkey);
	if (sigopts)
		sk_OPENSSL_STRING_free(sigopts);
	X509_REQ_free(rq);
	ASN1_INTEGER_free(sno);
	sk_ASN1_OBJECT_pop_free(trust, ASN1_OBJECT_free);
	sk_ASN1_OBJECT_pop_free(reject, ASN1_OBJECT_free);
	if (passin) OPENSSL_free(passin);
	return(ret);
	}

static ASN1_INTEGER *x509_load_serial(char *CAfile, char *serialfile, int create)
	{
	char *buf = NULL, *p;
	ASN1_INTEGER *bs = NULL;
	BIGNUM *serial = NULL;
	size_t len;

	len = ((serialfile == NULL)
		?(strlen(CAfile)+strlen(POSTFIX)+1)
		:(strlen(serialfile)))+1;
	buf=OPENSSL_malloc(len);
	if (buf == NULL) { BIO_printf(bio_err,"out of mem\n"); goto end; }
	if (serialfile == NULL)
		{
		BUF_strlcpy(buf,CAfile,len);
		for (p=buf; *p; p++)
			if (*p == '.')
				{
				*p='\0';
				break;
				}
		BUF_strlcat(buf,POSTFIX,len);
		}
	else
		BUF_strlcpy(buf,serialfile,len);

	serial = load_serial(buf, create, NULL);
	if (serial == NULL) goto end;

	if (!BN_add_word(serial,1))
		{ BIO_printf(bio_err,"add_word failure\n"); goto end; }

	if (!save_serial(buf, NULL, serial, &bs)) goto end;

 end:
	if (buf) OPENSSL_free(buf);
	BN_free(serial);
	return bs;
	}

static int x509_certify(X509_STORE *ctx, char *CAfile, const EVP_MD *digest,
	     		X509 *x, X509 *xca, EVP_PKEY *pkey,
			STACK_OF(OPENSSL_STRING) *sigopts,
	  		char *serialfile, int create,
	     		int days, int clrext, CONF *conf, char *section,
			ASN1_INTEGER *sno, int reqfile)
	{
	int ret=0;
	ASN1_INTEGER *bs=NULL;
	X509_STORE_CTX xsc;
	EVP_PKEY *upkey;

	upkey = X509_get_pubkey(xca);
	EVP_PKEY_copy_parameters(upkey,pkey);
	EVP_PKEY_free(upkey);

	if(!X509_STORE_CTX_init(&xsc,ctx,x,NULL))
		{
		BIO_printf(bio_err,"Error initialising X509 store\n");
		goto end;
		}
	if (sno) bs = sno;
	else if (!(bs = x509_load_serial(CAfile, serialfile, create)))
		goto end;

/*	if (!X509_STORE_add_cert(ctx,x)) goto end;*/

	/* NOTE: this certificate can/should be self signed, unless it was
	 * a certificate request in which case it is not. */
	X509_STORE_CTX_set_cert(&xsc,x);
	X509_STORE_CTX_set_flags(&xsc, X509_V_FLAG_CHECK_SS_SIGNATURE);
	if (!reqfile && X509_verify_cert(&xsc) <= 0)
		goto end;

	if (!X509_check_private_key(xca,pkey))
		{
		BIO_printf(bio_err,"CA certificate and CA private key do not match\n");
		goto end;
		}

	if (!X509_set_issuer_name(x,X509_get_subject_name(xca))) goto end;
	if (!X509_set_serialNumber(x,bs)) goto end;

	if (X509_gmtime_adj(X509_get_notBefore(x),0L) == NULL)
		goto end;

	/* hardwired expired */
	if (X509_time_adj_ex(X509_get_notAfter(x),days, 0, NULL) == NULL)
		goto end;

	if (clrext)
		{
		while (X509_get_ext_count(x) > 0) X509_delete_ext(x, 0);
		}

	if (conf)
		{
		X509V3_CTX ctx2;
#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
		X509_set_version(x, force_version);
#else
		X509_set_version(x,2); /* version 3 certificate */
#endif
                X509V3_set_ctx(&ctx2, xca, x, NULL, NULL, 0);
                X509V3_set_nconf(&ctx2, conf);
                if (!X509V3_EXT_add_nconf(conf, &ctx2, section, x)) goto end;
		}

	if (!do_X509_sign(bio_err, x, pkey, digest, sigopts))
		goto end;
	ret=1;
end:
	X509_STORE_CTX_cleanup(&xsc);
	if (!ret)
		ERR_print_errors(bio_err);
	if (!sno) ASN1_INTEGER_free(bs);
	return ret;
	}

static int callb(int ok, X509_STORE_CTX *ctx)
	{
	int err;
	X509 *err_cert;

	/* it is ok to use a self signed certificate
	 * This case will catch both the initial ok == 0 and the
	 * final ok == 1 calls to this function */
	err=X509_STORE_CTX_get_error(ctx);
	if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)
		return 1;

	/* BAD we should have gotten an error.  Normally if everything
	 * worked X509_STORE_CTX_get_error(ctx) will still be set to
	 * DEPTH_ZERO_SELF_.... */
	if (ok)
		{
		BIO_printf(bio_err,"error with certificate to be certified - should be self signed\n");
		return 0;
		}
	else
		{
		err_cert=X509_STORE_CTX_get_current_cert(ctx);
		print_name(bio_err, NULL, X509_get_subject_name(err_cert),0);
		BIO_printf(bio_err,"error with certificate - error %d at depth %d\n%s\n",
			err,X509_STORE_CTX_get_error_depth(ctx),
			X509_verify_cert_error_string(err));
		return 1;
		}
	}

/* self sign */
static int sign(X509 *x, EVP_PKEY *pkey, int days, int clrext, const EVP_MD *digest, 
						CONF *conf, char *section)
	{

	EVP_PKEY *pktmp;

	pktmp = X509_get_pubkey(x);
	EVP_PKEY_copy_parameters(pktmp,pkey);
	EVP_PKEY_save_parameters(pktmp,1);
	EVP_PKEY_free(pktmp);

	if (!X509_set_issuer_name(x,X509_get_subject_name(x))) goto err;
	if (X509_gmtime_adj(X509_get_notBefore(x),0) == NULL) goto err;

	/* Lets just make it 12:00am GMT, Jan 1 1970 */
	/* memcpy(x->cert_info->validity->notBefore,"700101120000Z",13); */
	/* 28 days to be certified */

	if (X509_gmtime_adj(X509_get_notAfter(x),(long)60*60*24*days) == NULL)
		goto err;

	if (!X509_set_pubkey(x,pkey)) goto err;
	if (clrext)
		{
		while (X509_get_ext_count(x) > 0) X509_delete_ext(x, 0);
		}
	if (conf)
		{
		X509V3_CTX ctx;
#ifdef OPENSSL_SSL_DEBUG_BROKEN_PROTOCOL
		X509_set_version(x, force_version);
#else
		X509_set_version(x,2); /* version 3 certificate */
#endif
                X509V3_set_ctx(&ctx, x, x, NULL, NULL, 0);
                X509V3_set_nconf(&ctx, conf);
                if (!X509V3_EXT_add_nconf(conf, &ctx, section, x)) goto err;
		}
	if (!X509_sign(x,pkey,digest)) goto err;
	return 1;
err:
	ERR_print_errors(bio_err);
	return 0;
	}

static int purpose_print(BIO *bio, X509 *cert, X509_PURPOSE *pt)
{
	int id, i, idret;
	char *pname;
	id = X509_PURPOSE_get_id(pt);
	pname = X509_PURPOSE_get0_name(pt);
	for (i = 0; i < 2; i++)
		{
		idret = X509_check_purpose(cert, id, i);
		BIO_printf(bio, "%s%s : ", pname, i ? " CA" : ""); 
		if (idret == 1) BIO_printf(bio, "Yes\n");
		else if (idret == 0) BIO_printf(bio, "No\n");
		else BIO_printf(bio, "Yes (WARNING code=%d)\n", idret);
		}
	return 1;
}
