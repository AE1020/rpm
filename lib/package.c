/** \ingroup header
 * \file lib/package.c
 */

#include "system.h"

#include <netinet/in.h>

#include <rpmio_internal.h>
#include <rpmlib.h>

#include "depends.h"

#include "misc.h"	/* XXX stripTrailingChar() */
#include "legacy.h"	/* XXX providePackageNVR() and compressFileList() */
#include "rpmlead.h"

#include "signature.h"
#include "debug.h"

#define	alloca_strdup(_s)	strcpy(alloca(strlen(_s)+1), (_s))

/*@access rpmTransactionSet@*/
/*@access Header@*/		/* XXX compared with NULL */

void headerMergeLegacySigs(Header h, const Header sig)
{
    HFD_t hfd = (HFD_t) headerFreeData;
    HAE_t hae = (HAE_t) headerAddEntry;
    HeaderIterator hi;
    int_32 tag, type, count;
    const void * ptr;
    int xx;

    /*@-mods@*/ /* FIX: undocumented modification of sig */
    for (hi = headerInitIterator(sig);
    /*@=mods@*/
        headerNextIterator(hi, &tag, &type, &ptr, &count);
        ptr = hfd(ptr, type))
    {
	switch (tag) {
	case RPMSIGTAG_SIZE:
	    tag = RPMTAG_SIGSIZE;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_LEMD5_1:
	    tag = RPMTAG_SIGLEMD5_1;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_PGP:
	    tag = RPMTAG_SIGPGP;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_LEMD5_2:
	    tag = RPMTAG_SIGLEMD5_2;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_MD5:
	    tag = RPMTAG_SIGMD5;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_GPG:
	    tag = RPMTAG_SIGGPG;
	    /*@switchbreak@*/ break;
	case RPMSIGTAG_PGP5:
	    tag = RPMTAG_SIGPGP5;
	    /*@switchbreak@*/ break;
	default:
	    if (!(tag >= HEADER_SIGBASE && tag < HEADER_TAGBASE))
		continue;
	    /*@switchbreak@*/ break;
	}
	if (ptr == NULL) continue;	/* XXX can't happen */
	if (!headerIsEntry(h, tag))
	    xx = hae(h, tag, type, ptr, count);
    }
    hi = headerFreeIterator(hi);
}

Header headerRegenSigHeader(const Header h)
{
    HFD_t hfd = (HFD_t) headerFreeData;
    Header sig = rpmNewSignature();
    HeaderIterator hi;
    int_32 tag, stag, type, count;
    const void * ptr;
    int xx;

    /*@-mods@*/ /* FIX: undocumented modification of h */
    for (hi = headerInitIterator(h);
    /*@=mods@*/
        headerNextIterator(hi, &tag, &type, &ptr, &count);
        ptr = hfd(ptr, type))
    {
	switch (tag) {
	case RPMTAG_SIGSIZE:
	    stag = RPMSIGTAG_SIZE;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGLEMD5_1:
	    stag = RPMSIGTAG_LEMD5_1;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGPGP:
	    stag = RPMSIGTAG_PGP;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGLEMD5_2:
	    stag = RPMSIGTAG_LEMD5_2;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGMD5:
	    stag = RPMSIGTAG_MD5;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGGPG:
	    stag = RPMSIGTAG_GPG;
	    /*@switchbreak@*/ break;
	case RPMTAG_SIGPGP5:
	    stag = RPMSIGTAG_PGP5;
	    /*@switchbreak@*/ break;
	default:
	    if (!(tag >= HEADER_SIGBASE && tag < HEADER_TAGBASE))
		continue;
	    stag = tag;
	    /*@switchbreak@*/ break;
	}
	if (ptr == NULL) continue;	/* XXX can't happen */
	if (!headerIsEntry(sig, stag))
	    xx = headerAddEntry(sig, stag, type, ptr, count);
    }
    hi = headerFreeIterator(hi);
    return sig;
}

#ifdef	DYING
/**
 * Retrieve package components from file handle.
 * @param fd		file handle
 * @param leadPtr	address of lead (or NULL)
 * @param sigs		address of signatures (or NULL)
 * @param hdrPtr	address of header (or NULL)
 * @return		rpmRC return code
 */
static rpmRC readPackageHeaders(FD_t fd,
		/*@null@*/ /*@out@*/ struct rpmlead * leadPtr, 
		/*@null@*/ /*@out@*/ Header * sigs,
		/*@null@*/ /*@out@*/ Header * hdrPtr)
	/*@globals fileSystem@*/
	/*@modifies fd, *leadPtr, *sigs, *hdrPtr, fileSystem @*/
{
    Header hdrBlock;
    struct rpmlead leadBlock;
    Header * hdr = NULL;
    struct rpmlead * lead;
    struct stat sb;
    rpmRC rc;

    hdr = hdrPtr ? hdrPtr : &hdrBlock;
    lead = leadPtr ? leadPtr : &leadBlock;

    memset(&sb, 0, sizeof(sb));
    (void) fstat(Fileno(fd), &sb);
    /* if fd points to a socket, pipe, etc, sb.st_size is *always* zero */
    if (S_ISREG(sb.st_mode) && sb.st_size < sizeof(*lead)) return 1;

    if (readLead(fd, lead))
	return RPMRC_FAIL;

    if (lead->magic[0] != RPMLEAD_MAGIC0 || lead->magic[1] != RPMLEAD_MAGIC1 ||
	lead->magic[2] != RPMLEAD_MAGIC2 || lead->magic[3] != RPMLEAD_MAGIC3)
	return RPMRC_BADMAGIC;

    switch (lead->major) {
    case 1:
	rpmError(RPMERR_NEWPACKAGE,
	    _("packaging version 1 is not supported by this version of RPM\n"));
	return RPMRC_FAIL;
	/*@notreached@*/ break;
    case 2:
    case 3:
    case 4:
	rc = rpmReadSignature(fd, sigs, lead->signature_type);
	if (rc == RPMRC_FAIL)
	    return rc;
	*hdr = headerRead(fd, (lead->major >= 3)
			  ? HEADER_MAGIC_YES : HEADER_MAGIC_NO);
	if (*hdr == NULL) {
	    if (sigs != NULL)
		*sigs = rpmFreeSignature(*sigs);
	    return RPMRC_FAIL;
	}

	/* Convert legacy headers on the fly ... */
	legacyRetrofit(*hdr, lead);
	break;
    default:
	rpmError(RPMERR_NEWPACKAGE, _("only packaging with major numbers <= 4 "
		"is supported by this version of RPM\n"));
	return RPMRC_FAIL;
	/*@notreached@*/ break;
    } 

    if (hdrPtr == NULL)
	*hdr = headerFree(*hdr);
    
    return RPMRC_OK;
}
#endif

int rpmReadPackageFile(rpmTransactionSet ts, FD_t fd,
		const char * fn, Header * hdrp)
{
    byte buf[8*BUFSIZ];
    ssize_t count;
    struct rpmlead * l = alloca(sizeof(*l));
    Header sig;
    Header h = NULL;
    int hmagic;
    rpmRC rc = RPMRC_FAIL;	/* assume failure */
    int xx;
    int i;

    {	struct stat st;
	memset(&st, 0, sizeof(st));
	(void) fstat(Fileno(fd), &st);
	/* if fd points to a socket, pipe, etc, st.st_size is *always* zero */
	if (S_ISREG(st.st_mode) && st.st_size < sizeof(*l))
	    goto exit;
    }

    memset(l, 0, sizeof(*l));
    if (readLead(fd, l)) {
	rpmError(RPMERR_READLEAD, _("%s: readLead failed\n"), fn);
	goto exit;
    }

    if (l->magic[0] != RPMLEAD_MAGIC0 || l->magic[1] != RPMLEAD_MAGIC1
     || l->magic[2] != RPMLEAD_MAGIC2 || l->magic[3] != RPMLEAD_MAGIC3) {
	rpmError(RPMERR_READLEAD, _("%s: bad magic\n"), fn);
	rc = RPMRC_BADMAGIC;
	goto exit;
    }

    switch (l->major) {
    case 1:
	rpmError(RPMERR_NEWPACKAGE,
	    _("packaging version 1 is not supported by this version of RPM\n"));
	goto exit;
	/*@notreached@*/ break;
    case 2:
    case 3:
    case 4:
	break;
    default:
	rpmError(RPMERR_NEWPACKAGE, _("only packaging with major numbers <= 4 "
		"is supported by this version of RPM\n"));
	goto exit;
	/*@notreached@*/ break;
    }

    rc = rpmReadSignature(fd, &sig, l->signature_type);
    if (!(rc == RPMRC_OK || rc == RPMRC_BADSIZE)) {
	rpmError(RPMERR_SIGGEN, _("%s: rpmReadSignature failed\n"), fn);
	goto exit;
    }
    if (sig == NULL) {
	rpmError(RPMERR_SIGGEN, _("%s: No signature available\n"), fn);
	rc = RPMRC_FAIL;
	goto exit;
    }

    if (ts->need_payload)	/* leave fd ready to install payload */
	ts->sigtag = 0;
    else if (headerIsEntry(sig, RPMSIGTAG_GPG))
	ts->sigtag = RPMSIGTAG_GPG;
    else if (headerIsEntry(sig, RPMSIGTAG_PGP))
	ts->sigtag = RPMSIGTAG_PGP;
    else if (headerIsEntry(sig, RPMSIGTAG_MD5))
	ts->sigtag = RPMSIGTAG_MD5;
    else
	ts->sigtag = 0;

    /*@-type@*/ /* FIX: cast? */
    if (ts->sigtag == RPMSIGTAG_GPG)
	fdInitDigest(fd, PGPHASHALGO_SHA1, 0);
    else if (ts->sigtag == RPMSIGTAG_PGP || ts->sigtag == RPMSIGTAG_MD5)
	fdInitDigest(fd, PGPHASHALGO_MD5, 0);
    /*@=type@*/

    hmagic = ((l->major >= 3) ? HEADER_MAGIC_YES : HEADER_MAGIC_NO);
    h = headerRead(fd, hmagic);
    if (h == NULL) {
	rpmError(RPMERR_FREAD, _("%s: headerRead failed\n"), fn);
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Any signatures to check? */
    if (ts->sigtag == 0) {
	rc = RPMRC_OK;
	goto exit;
    }

    ts->dig = pgpNewDig();
    ts->dig->nbytes = headerSizeof(h, hmagic);

    /* Read the compressed payload. */
    while ((count = Fread(buf, sizeof(buf[0]), sizeof(buf), fd)) > 0)
	ts->dig->nbytes += count;

    if (count < 0) {
	rpmError(RPMERR_FREAD, _("%s: Fread failed: %s\n"), fn, Fstrerror(fd));
	rc = RPMRC_FAIL;
	goto exit;
    }
    ts->dig->nbytes += count;

    xx = headerGetEntry(sig, ts->sigtag, &ts->sigtype,
		(void **) &ts->sig, &ts->siglen);

    xx = pgpPrtPkts(ts->sig, ts->siglen, ts->dig, rpmIsDebug());

    /*@-type@*/ /* FIX: cast? */
    for (i = fd->ndigests - 1; i >= 0; i--) {
	FDDIGEST_t fddig = fd->digests + i;
	if (fddig->hashctx == NULL)
	    continue;
	if (fddig->hashalgo == PGPHASHALGO_MD5) {
	    /*@-branchstate@*/
	    if (ts->dig->md5ctx != NULL)
		(void) rpmDigestFinal(ts->dig->md5ctx, NULL, NULL, 0);
	    /*@=branchstate@*/
	    ts->dig->md5ctx = fddig->hashctx;
	    fddig->hashctx = NULL;
	    continue;
	}
	if (fddig->hashalgo == PGPHASHALGO_SHA1) {
	    /*@-branchstate@*/
	    if (ts->dig->sha1ctx != NULL)
		(void) rpmDigestFinal(ts->dig->sha1ctx, NULL, NULL, 0);
	    /*@=branchstate@*/
	    ts->dig->sha1ctx = fddig->hashctx;
	    fddig->hashctx = NULL;
	    continue;
	}
    }
    /*@=type@*/

/** @todo Implement disable/enable/warn/error/anal policy. */

    buf[0] = '\0';
    switch (rpmVerifySignature(ts, buf)) {
    case RPMSIG_OK:		/*!< Signature is OK. */
	rpmMessage(RPMMESS_VERBOSE, "%s: %s", fn, buf);
	rc = RPMRC_OK;
	break;
    case RPMSIG_UNKNOWN:	/*!< Signature is unknown. */
    case RPMSIG_NOKEY:		/*!< Key is unavailable. */
    case RPMSIG_NOTTRUSTED:	/*!< Signature is OK, but key is not trusted. */
	rpmMessage(RPMMESS_WARNING, "%s: %s", fn, buf);
	rc = RPMRC_OK;
	break;
    default:
    case RPMSIG_BAD:		/*!< Signature does not verify. */
	rpmMessage(RPMMESS_ERROR, "%s: %s", fn, buf);
	rc = RPMRC_OK;
	break;
    }

exit:
    if (rc == 0 && hdrp != NULL) {
	/* Convert legacy headers on the fly ... */
	legacyRetrofit(h, l);
	
	/* Append (and remap) signature tags. */
	headerMergeLegacySigs(h, sig);

	/* Bump reference count for return. */
	*hdrp = headerLink(h);
    }
    h = headerFree(h);
    if (ts->sig != NULL)
	ts->sig = headerFreeData(ts->sig, ts->sigtype);
    if (ts->dig != NULL)
	ts->dig = pgpFreeDig(ts->dig);
    sig = rpmFreeSignature(sig);
    return rc;
}
