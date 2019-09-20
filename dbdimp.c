/*
	vim: sw=4:ts=8
	dbdimp.c

	Copyright (c) 1994-2006  Tim Bunce  Ireland
	Copyright (c) 2006-2008  John Scoles (The Pythian Group), Canada

	See the COPYRIGHT section in the Oracle.pm file for terms.

*/

#ifdef WIN32
#define strcasecmp strcmpi
#endif

#ifdef __CYGWIN32__
#include "w32api/windows.h"
#include "w32api/winbase.h"
#endif /* __CYGWIN32__ */

#include "Oracle.h"

/* XXX DBI should provide a better version of this */
#define IS_DBI_HANDLE(h) \
	(SvROK(h) && SvTYPE(SvRV(h)) == SVt_PVHV && \
	SvRMAGICAL(SvRV(h)) && (SvMAGIC(SvRV(h)))->mg_type == 'P')

#ifndef SvPOK_only_UTF8
#define SvPOK_only_UTF8(sv) SvPOK_only(sv)
#endif

DBISTATE_DECLARE;

int ora_fetchtest;         /* internal test only, not thread safe */
int is_extproc	  	  = 0; /* not ProC but ExtProc.pm */
int dbd_verbose		  = 0; /* DBD only debugging*/
int oci_warn		  = 0; /* show oci warnings */
int ora_objects		  = 0; /* get oracle embedded objects as instance of DBD::Oracle::Object */
int ora_ncs_buff_mtpl = 4; /* a mulitplyer for ncs clob buffers */

/* bitflag constants for figuring out how to handle utf8 for array binds */
#define ARRAY_BIND_NATIVE 0x01
#define ARRAY_BIND_UTF8   0x02
#define ARRAY_BIND_MIXED  (ARRAY_BIND_NATIVE|ARRAY_BIND_UTF8)


ub2 charsetid		= 0;
ub2 ncharsetid		= 0;
ub2 us7ascii_csid	= 1;
ub2 utf8_csid		= 871;
ub2 al32utf8_csid	= 873;
ub2 al16utf16_csid	= 2000;


typedef struct sql_fbh_st sql_fbh_t;
struct sql_fbh_st {
	int dbtype;
	int prec;
	int scale;
};
static sql_fbh_t ora2sql_type _((imp_fbh_t* fbh));
static void disable_taf(imp_dbh_t *imp_dbh);
static int enable_taf(SV *dbh, imp_dbh_t *imp_dbh);

void ora_free_phs_contents _((imp_sth_t *imp_sth, phs_t *phs));
static void dump_env_to_trace(imp_dbh_t *imp_dbh);

static sb4
oci_error_get(imp_xxh_t *imp_xxh,
              OCIError *errhp, sword status, char *what, SV *errstr, int debug)
{
	dTHX;
	text errbuf[1024];
	ub4 recno		= 0;
	sb4 errcode		= 0;
	sb4 eg_errcode	= 0;
	sword eg_status;
	if (!SvOK(errstr))
		sv_setpv(errstr,"");
	if (!errhp) {
		sv_catpv(errstr, oci_status_name(status));
		if (what) {
			sv_catpv(errstr, " ");
			sv_catpv(errstr, what);
		}
		return status;
	}

	while( ++recno
           && OCIErrorGet_log_stat(imp_xxh, errhp, recno, (text*)NULL, &eg_errcode, errbuf,
		(ub4)sizeof(errbuf), OCI_HTYPE_ERROR, eg_status) != OCI_NO_DATA
           && eg_status != OCI_INVALID_HANDLE
           && recno < 100
	) {
		if (debug >= 4 || recno>1/*XXX temp*/  || dbd_verbose >= 4 )
			PerlIO_printf(DBIc_LOGPIO(imp_xxh),
                          "	OCIErrorGet after %s (er%ld:%s): %d, %ld: %s\n",
                          what ? what : "<NULL>", (long)recno,
                          (eg_status==OCI_SUCCESS) ? "ok" : oci_status_name(eg_status),
                          status, (long)eg_errcode, errbuf);
		errcode = eg_errcode;
		sv_catpv(errstr, (char*)errbuf);
		if (*(SvEND(errstr)-1) == '\n')
			--SvCUR(errstr);
	}

	if (what || status != OCI_ERROR) {
		sv_catpv(errstr, (debug<0) ? " (" : " (DBD ");
		sv_catpv(errstr, oci_status_name(status));
		if (what) {
			sv_catpv(errstr, ": ");
			sv_catpv(errstr, what);
		}
		sv_catpv(errstr, ")");
	}
	return errcode;
}

static int
GetRegKey(char *key, char *val, char *data, unsigned long *size)
{
#ifdef WIN32
	unsigned long len = *size - 1;
	HKEY hKey;
	long ret;

	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &hKey);
	if (ret != ERROR_SUCCESS)
		return 0;
	ret = RegQueryValueEx(hKey, val, NULL, NULL, data, size);
	RegCloseKey(hKey);
	if ((ret != ERROR_SUCCESS) || (*size >= len))
		return 0;
	return 1;
#else
	/* For gcc not to warn on unused parameters. */
	if( key ){}
	if( val ){}
	if( data ){}
	if( size ){}
	return 0;
#endif
}

char *
ora_env_var(char *name, char *buf, unsigned long size)
{
#define WIN32_REG_BUFSIZE 80
	dTHX;
	char last_home_id[WIN32_REG_BUFSIZE+1];
	char ora_home_key[WIN32_REG_BUFSIZE+1];
	unsigned long len = WIN32_REG_BUFSIZE;
	char *e = getenv(name);
	if (e)
	return e;
	if (!GetRegKey("SOFTWARE\\ORACLE\\ALL_HOMES", "LAST_HOME", last_home_id, &len))
	return Nullch;
	last_home_id[2] = 0;
	sprintf(ora_home_key, "SOFTWARE\\ORACLE\\HOME%s", last_home_id);
	size -= 1; /* allow room for null termination */
	if (!GetRegKey(ora_home_key, name, buf, &size))
	return Nullch;
	buf[size] = 0;
	return buf;
}

#ifdef __CYGWIN32__
/* Under Cygwin there are issues with setting environment variables
 * at runtime such that Windows-native libraries loaded by a Cygwin
 * process can see those changes.
 *
 * Cygwin maintains its own cache of environment variables, and also
 * only writes to the Windows environment using the "_putenv" win32
 * call. This call writes to a Windows C runtime cache, rather than
 * the true process environment block.
 *
 * In order to change environment variables so that the Oracle client
 * DLL can see the change, the win32 function SetEnvironmentVariable
 * must be called. This function gives an interface to that API.
 *
 * It is only available when building under Cygwin, and is used by
 * the testsuite.
 *
 * Whilst it could be called by end users, it should be used with
 * caution, as it bypasses the environment variable conversions that
 * Cygwin typically performs.
 */
void
ora_cygwin_set_env(char *name, char *value)
{
	SetEnvironmentVariable(name, value);
}
#endif /* __CYGWIN32__ */


void
dbd_init(dbistate_t *dbistate)
{
	dTHX;
	DBIS = dbistate;
	dbd_init_oci(dbistate);
}


void
dbd_dr_destroy(SV *drh, imp_drh_t *imp_drh)
{
	dTHX;
	sword status;

	/* We rely on the DBI dispatcher to destroy all child handles before we get here (DBI >= 1.623). */

	if (imp_drh->leak_handles) {
		/* By using ithread, handles will leak in dbd_dr_destroy() */
		return;
	}

#ifdef ORA_OCI_112
	/* Free session pool resources. */
	if (imp_drh->pool_hv) {
		HE *pool_he;
		hv_iterinit(imp_drh->pool_hv);
		while ((pool_he = hv_iternext(imp_drh->pool_hv))) {
			session_pool_t *pool = (session_pool_t*)SvPVX(HeVAL(pool_he));
			/* Only destroy the session pool if there are no active sessions left.
			   If there are active sessions left, this is because "InactiveDestroy"
			   is set on one or more db handles. */
			if (!pool->active_sessions) {
				OCISessionPoolDestroy_log_stat(imp_drh, pool->poolhp, pool->errhp, status);
			}
			OCIHandleFree_log_stat(imp_drh, pool->poolhp, OCI_HTYPE_SPOOL, status);
			OCIHandleFree_log_stat(imp_drh, pool->errhp, OCI_HTYPE_ERROR, status);
			OCIHandleFree_log_stat(imp_drh, pool->envhp, OCI_HTYPE_ENV, status);
		}
		hv_undef(imp_drh->pool_hv);
	}

	if (imp_drh->charset_hv) {
		hv_undef(imp_drh->charset_hv);
	}
#endif
}


int
dbd_discon_all(SV *drh, imp_drh_t *imp_drh)
{
	dTHR;
	dTHX;

    /* The disconnect_all concept is flawed and needs more work */
	if (!PL_dirty && !SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) {
        DBIh_SET_ERR_CHAR(drh, (imp_xxh_t*)imp_drh, Nullch, 1, "disconnect_all not implemented", Nullch, Nullch);
        return FALSE;
	}
	return FALSE;
}


void
dbd_fbh_dump(imp_sth_t *imp_sth, imp_fbh_t *fbh, int i, int aidx)
{
	dTHX;
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	fbh %d: '%s'\t%s, ",
		i, fbh->name, (fbh->nullok) ? "NULLable" : "NO null ");
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "otype %3d->%3d, dbsize %ld/%ld, p%d.s%d\n",
		fbh->dbtype, fbh->ftype, (long)fbh->dbsize,(long)fbh->disize,
		fbh->prec, fbh->scale);
	if (fbh->fb_ary) {
        PerlIO_printf(DBIc_LOGPIO(imp_sth), "	  out: ftype %d, bufl %d. indp %d, rlen %d, rcode %d\n",
		fbh->ftype, fbh->fb_ary->bufl, fbh->fb_ary->aindp[aidx],
		fbh->fb_ary->arlen[aidx], fbh->fb_ary->arcode[aidx]);
	}
}

int
ora_dbtype_is_long(int dbtype)
{
	/* Is it a LONG, LONG RAW, LONG VARCHAR or LONG VARRAW type?	*/
	/* Return preferred type code to use if it's a long, else 0.	*/
	if (dbtype == 8 || dbtype == 24)	/* LONG or LONG RAW		*/
	return dbtype;			/*		--> same	*/
	if (dbtype == 94)			/* LONG VARCHAR			*/
	return 8;			/*		--> LONG	*/
	if (dbtype == 95)			/* LONG VARRAW			*/
	return 24;			/*		--> LONG RAW	*/
	return 0;
}

static int
oratype_bind_ok(int dbtype) /* It's a type we support for placeholders */
{
	/* basically we support types that can be returned as strings */
	switch(dbtype) {
	case  1:	/* VARCHAR2	*/
	case  2:	/* NVARCHAR2	*/
	case  5:	/* STRING	*/
	case  8:	/* LONG		*/
	case 21:	/* BINARY FLOAT os-endian */
	case 22:	/* BINARY DOUBLE os-endian */
	case 23:	/* RAW		*/
	case 24:	/* LONG RAW	*/
	case 96:	/* CHAR		*/
	case 97:	/* CHARZ	*/
	case 100:	/* BINARY FLOAT oracle-endian */
	case 101:	/* BINARY DOUBLE oracle-endian */
	case 106:	/* MLSLABEL	*/
	case 102:	/* SQLT_CUR	OCI 7 cursor variable	*/
	case 112:	/* SQLT_CLOB / long	*/
	case 113:	/* SQLT_BLOB / long	*/
	case 116:	/* SQLT_RSET	OCI 8 cursor variable	*/
 	case ORA_VARCHAR2_TABLE: /* 201 */
	case ORA_NUMBER_TABLE:	/* 202 */
	case ORA_XMLTYPE:		/* SQLT_NTY   must be careful here as its value (108) is the same for an embedded object Well really only XML clobs not embedded objects  */
	return 1;
	}
	return 0;
}

#ifdef THIS_IS_NOT_CURRENTLY_USED
static int
oratype_rebind_ok(int dbtype) /* all are vrcar any way so just use it */
{
	/* basically we support types that can be returned as strings */
	switch(dbtype) {
	case  1:	/* VARCHAR2	*/
	case  2:	/* NVARCHAR2	*/
	case  5:	/* STRING	*/
	case  8:	/* LONG		*/
	case 21:	/* BINARY FLOAT os-endian */
	case 22:	/* BINARY DOUBLE os-endian */
	case 23:	/* RAW		*/
	case 24:	/* LONG RAW	*/
	case 96:	/* CHAR		*/
	case 97:	/* CHARZ	*/
	case 100:	/* BINARY FLOAT oracle-endian */
	case 101:	/* BINARY DOUBLE oracle-endian */
	case 106:	/* MLSLABEL	*/
	case 102:	/* SQLT_CUR	OCI 7 cursor variable	*/
	case 116:	/* SQLT_RSET	OCI 8 cursor variable	*/
 	case ORA_VARCHAR2_TABLE: /* 201 */
	case ORA_NUMBER_TABLE:	/* 202 */
	case ORA_XMLTYPE:		/* SQLT_NTY   must be carefull here as its value (108) is the same for an embedded object Well realy only XML clobs not embedded objects  */
	case 113:	/* SQLT_BLOB / long	*/
		return SQLT_BIN;
	case 112:	/* SQLT_CLOB / long	*/
		return SQLT_CHR;
	}

	return dbtype;
}
#endif /* THIS_IS_NOT_CURRENTLY_USED */
/* --- allocate and free oracle oci 'array' buffers --- */

/* --- allocate and free oracle oci 'array' buffers for callback--- */

fb_ary_t *
fb_ary_cb_alloc(ub4 piece_size, ub4 max_len, int size)
{
	fb_ary_t *fb_ary;
	/* these should be reworked to only to one Newz()	*/
	/* and setup the pointers in the head fb_ary struct	*/
	Newz(42, fb_ary, sizeof(fb_ary_t), fb_ary_t);
	Newz(42, fb_ary->abuf,		size * piece_size, ub1);
	Newz(42, fb_ary->cb_abuf,	size * max_len, ub1);
	Newz(42, fb_ary->aindp,(unsigned)size,sb2);
	Newz(42, fb_ary->arlen,(unsigned)size,ub2);
	Newz(42, fb_ary->arcode,(unsigned)size,ub2);
	fb_ary->bufl = piece_size;
	fb_ary->cb_bufl = max_len;
	return fb_ary;
}


/* --- allocate and free oracle oci 'array' buffers --- */

fb_ary_t *
fb_ary_alloc(ub4 bufl, int size)
{
	fb_ary_t *fb_ary;
	/* these should be reworked to only to one Newz()	*/
	/* and setup the pointers in the head fb_ary struct	*/
	Newz(42, fb_ary, sizeof(fb_ary_t), fb_ary_t);
	Newz(42, fb_ary->abuf,	size * bufl, ub1);
	Newz(42, fb_ary->aindp,	(unsigned)size,sb2);
	Newz(42, fb_ary->arlen,	(unsigned)size,ub2);
	Newz(42, fb_ary->arcode,(unsigned)size,ub2);
	fb_ary->bufl = bufl;
	/* fb_ary->cb_bufl = bufl;*/
	return fb_ary;
}

void
fb_ary_free(fb_ary_t *fb_ary)
{
	Safefree(fb_ary->abuf);
	Safefree(fb_ary->aindp);
	Safefree(fb_ary->arlen);
	Safefree(fb_ary->arcode);
	Safefree(fb_ary->cb_abuf);
	Safefree(fb_ary);

}

#ifdef ORA_OCI_112
/* Helper functions to fetch cached session pool. */
static SV *
pool_key(imp_dbh_t *imp_dbh, char *dbname, char *uid, char *pwd, int csid, int ncsid)
{
	return newSVpvf("%s/%s@%s class=%.*s,csid=%d,ncsid=%d", uid, pwd, dbname, imp_dbh->pool_classl, imp_dbh->pool_class, csid, ncsid);
}

static session_pool_t *
pool_fetch(imp_drh_t *imp_drh, SV *key)
{
	dTHX;
	HE *pool_he = hv_fetch_ent(imp_drh->pool_hv, key, 0, 0);
	return pool_he ? (session_pool_t *)SvPVX(HeVAL(pool_he)) : NULL;
}
#endif


/* ================================================================== */


int
dbd_db_login(SV *dbh, imp_dbh_t *imp_dbh, char *dbname, char *uid, char *pwd)
{
	return dbd_db_login6(dbh, imp_dbh, dbname, uid, pwd, Nullsv);
}


/* from shared.xs */
typedef struct {
	SV	*sv; /* The actual SV - in shared space */
	/* we don't need the following two */
	/*recursive_lock_t	lock; */
	/*perl_cond		   user_cond;*/	  /* For user-level conditions */
} shared_sv;



int
dbd_db_login6(SV *dbh, imp_dbh_t *imp_dbh, char *dbname, char *uid, char *pwd, SV *attr)
{
	dTHR;
	dTHX;
	sword status;
	SV **svp;
	shared_sv * shared_dbh_ssv = NULL ;
	imp_dbh_t * shared_dbh	 = NULL ;
	D_imp_drh_from_dbh;
	ub2 new_charsetid = 0;
	ub2 new_ncharsetid = 0;
#if defined(USE_ITHREADS) && defined(PERL_MAGIC_shared_scalar)
	SV **	shared_dbh_priv_svp ;
	SV *	shared_dbh_priv_sv ;
	STRLEN	shared_dbh_len  = 0 ;
#endif

#ifdef ORA_OCI_112
	session_pool_t *pool = NULL;

	/*check to see if the user is connecting with DRCP */
	if (DBD_ATTRIB_TRUE(attr,"ora_drcp",8,svp))
		imp_dbh->using_drcp = 1;

	/* some connection pool attributes  */

	if ((svp=DBD_ATTRIB_GET_SVP(attr, "ora_drcp_class", 14)) && SvOK(*svp)) {
		STRLEN  svp_len;
		if (!SvPOK(*svp))
			croak("ora_drcp_class is not a string");
		imp_dbh->pool_class = (text *) SvPV (*svp, svp_len );
		imp_dbh->pool_classl= (ub4) svp_len;
    }
    if (DBD_ATTRIB_TRUE(attr,"ora_drcp_min",12,svp))
		DBD_ATTRIB_GET_IV( attr, "ora_drcp_min",  12, svp, imp_dbh->pool_min);
	if (DBD_ATTRIB_TRUE(attr,"ora_drcp_max",12,svp))
		DBD_ATTRIB_GET_IV( attr, "ora_drcp_max",  12, svp, imp_dbh->pool_max);
	if (DBD_ATTRIB_TRUE(attr,"ora_drcp_incr",13,svp))
		DBD_ATTRIB_GET_IV( attr, "ora_drcp_incr",  13, svp, imp_dbh->pool_incr);
	if (DBD_ATTRIB_TRUE(attr,"ora_drcp_rlb",12,svp))
		DBD_ATTRIB_GET_IV( attr, "ora_drcp_rlb",  12, svp, imp_dbh->pool_rlb);

	imp_dbh->driver_name = "DBD::Oracle : " VERSION;
#endif

    /* TAF Events */
    if ((svp=DBD_ATTRIB_GET_SVP(attr, "ora_taf_function",  16)) && SvOK(*svp)) {
        if ((SvROK(*svp) && (SvTYPE(SvRV(*svp)) == SVt_PVCV)) ||
            (SvPOK(*svp))) {
            imp_dbh->taf_function = newSVsv(*svp);
        } else {
            croak("ora_taf_function needs to be a string or code reference");
        }
        /* avoid later STORE: */
        /* See DBI::DBB problem with ATTRIB_DELETE until DBI 1.607 */
        /* DBD_ATTRIB_DELETE(attr, "ora_taf_function", 16); */
        (void)hv_delete((HV*)SvRV(attr), "ora_taf_function", 16, G_DISCARD);
	}

    imp_dbh->server_version = 0;

	/* check to see if DBD_verbose or ora_verbose is set*/
	if (DBD_ATTRIB_TRUE(attr,"dbd_verbose",11,svp))
		DBD_ATTRIB_GET_IV(  attr, "dbd_verbose",  11, svp, dbd_verbose);
	if (DBD_ATTRIB_TRUE(attr,"ora_verbose",11,svp))
		DBD_ATTRIB_GET_IV(  attr, "ora_verbose",  11, svp, dbd_verbose);

	if (DBIc_DBISTATE(imp_dbh)->debug >= 6 || dbd_verbose >= 6 )
		dump_env_to_trace(imp_dbh);

	/* dbi_imp_data code adapted from DBD::mysql */
	if (DBIc_has(imp_dbh, DBIcf_IMPSET)) {
		/* dbi_imp_data from take_imp_data */
		if (DBIc_has(imp_dbh, DBIcf_ACTIVE)) {
			if (DBIc_DBISTATE(imp_dbh)->debug >= 2 || dbd_verbose >= 3 )
				PerlIO_printf(DBIc_LOGPIO(imp_dbh), "dbd_db_login6 skip connect\n");
			/* tell our parent we've adopted an active child */
			++DBIc_ACTIVE_KIDS(DBIc_PARENT_COM(imp_dbh));

			return 1;
		}
		/* not ACTIVE so connect not skipped */
		if (DBIc_DBISTATE(imp_dbh)->debug >= 2 || dbd_verbose >= 3 )
			PerlIO_printf(DBIc_LOGPIO(imp_dbh),
				"dbd_db_login6 IMPSET but not ACTIVE so connect not skipped\n");
	}

	imp_dbh->envhp = imp_drh->envhp;	/* will be NULL on first connect */

#if defined(USE_ITHREADS) && defined(PERL_MAGIC_shared_scalar)
	shared_dbh_priv_svp = (DBD_ATTRIB_OK(attr)?hv_fetch((HV*)SvRV(attr), "ora_dbh_share", 13, 0):NULL) ;
	shared_dbh_priv_sv = shared_dbh_priv_svp?*shared_dbh_priv_svp:NULL ;

	if (shared_dbh_priv_sv && SvROK(shared_dbh_priv_sv))
		shared_dbh_priv_sv = SvRV(shared_dbh_priv_sv) ;

	if (shared_dbh_priv_sv) {
		MAGIC * mg ;

		SvLOCK (shared_dbh_priv_sv) ;

		/* some magic from shared.xs (no public api yet :-( */
		mg = mg_find(shared_dbh_priv_sv, PERL_MAGIC_shared_scalar) ;

		shared_dbh_ssv = (shared_sv * )(mg?mg -> mg_ptr:NULL) ;  /*sharedsv_find(*shared_dbh_priv_sv) ;*/

		if (!shared_dbh_ssv)
			croak ("value of ora_dbh_share must be a scalar that is shared") ;

		shared_dbh 		= (imp_dbh_t *)SvPVX(shared_dbh_ssv -> sv) ;
		shared_dbh_len 	= SvCUR((shared_dbh_ssv -> sv)) ;

		if (shared_dbh_len > 0 && shared_dbh_len != sizeof (imp_dbh_t))
			croak ("Invalid value for ora_dbh_share") ;

		if (shared_dbh_len == sizeof (imp_dbh_t)) {
		/* initialize from shared data */
			memcpy (((char *)imp_dbh) + DBH_DUP_OFF, ((char *)shared_dbh) + DBH_DUP_OFF, DBH_DUP_LEN) ;
			shared_dbh -> refcnt++ ;
			imp_dbh -> shared_dbh_priv_sv = shared_dbh_priv_sv ;
			imp_dbh -> shared_dbh		 = shared_dbh ;
			if (DBIc_DBISTATE(imp_dbh)->debug >= 2 || dbd_verbose >= 3 )
				PerlIO_printf(DBIc_LOGPIO(imp_dbh), "	dbd_db_login: use shared Oracle database handles.\n");
		} else {
			shared_dbh = NULL ;
		}

		/* By using ithread, handles will leak in dbd_dr_destroy() */
		imp_drh->leak_handles = 1;
	}
#endif

	imp_dbh->get_oci_handle = oci_db_handle;

#ifdef ORA_OCI_112
	if (!imp_dbh->using_drcp) {
#endif
		if ((svp=DBD_ATTRIB_GET_SVP(attr, "ora_envhp", 9)) && SvOK(*svp)) {
			if (!SvTRUE(*svp)) {
				imp_dbh->envhp = NULL; /* force new environment */
			}
		}
		/* Test if a cached environment handle it still usable (see RT46739) */
		if (imp_dbh->envhp) {
			OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_dbh->errhp, OCI_HTYPE_ERROR, status);
			if (status != OCI_SUCCESS) {
				imp_dbh->envhp = NULL;
			}
		}
#ifdef ORA_OCI_112
	}
	else if (!shared_dbh) {
		/* Try to find session pool in cache. */
		imp_dbh->envhp = NULL;

		if (!imp_drh->charset_hv) {
			imp_drh->charset_hv = newHV();
		}
		if (!imp_drh->pool_hv) {
			imp_drh->pool_hv = newHV();
		}

		/* Get charset and ncharset IDs. */
		if ((svp = DBD_ATTRIB_GET_SVP(attr, "ora_charset", 11))) {
			/* Charset name from parameter; try looking up previously used ID. */
			HE *charset_he = hv_fetch_ent(imp_drh->charset_hv, *svp, 0, 0);
			charsetid = charset_he ? SvIV(HeVAL(charset_he)) : 0;
		}
		else {
			/* Get charset ID from the NLS environment. */
			size_t rsize;
			OCINlsEnvironmentVariableGet_log_stat(imp_dbh, &charsetid, 0, OCI_NLS_CHARSET_ID, 0, &rsize, status);
			if (status != OCI_SUCCESS) {
				oci_error(dbh, NULL, status,
					"OCINlsEnvironmentVariableGet(OCI_NLS_CHARSET_ID) Check NLS settings etc.");
				return 0;
			}
		}
		if ((svp = DBD_ATTRIB_GET_SVP(attr, "ora_ncharset", 12))) {
			/* Charset name from parameter; try looking up previously used ID. */
			HE *charset_he = hv_fetch_ent(imp_drh->charset_hv, *svp, 0, 0);
			ncharsetid = charset_he ? SvIV(HeVAL(charset_he)) : 0;
		}
		else {
			/* Get charset ID from the NLS environment. */
			size_t rsize;
			OCINlsEnvironmentVariableGet_log_stat(imp_dbh, &ncharsetid, 0, OCI_NLS_NCHARSET_ID, 0, &rsize, status);
			if (status != OCI_SUCCESS) {
				oci_error(dbh, NULL, status,
					"OCINlsEnvironmentVariableGet(OCI_NLS_NCHARSET_ID) Check NLS settings etc.");
					return 0;
			}
		}

		if (charsetid && ncharsetid) {
			/* Look up session pool initialized with the same dbname, uid/pwd, connection class, and charsets. */
			SV *key_sv = pool_key(imp_dbh, dbname, uid, pwd, charsetid, ncharsetid);

			if ((pool = pool_fetch(imp_drh, key_sv))) {
				imp_dbh->pool = pool;
				imp_dbh->envhp = pool->envhp;
			}

			sv_free(key_sv);
		}
	}
#endif

    if (!imp_dbh->envhp ) {
		SV **init_mode_sv;
		ub4 init_mode = OCI_OBJECT;/* needed for LOBs (8.0.4)	*/

		if (DBD_ATTRIB_TRUE(attr, "ora_events", 10, svp))
			init_mode |= OCI_EVENTS; /* Needed for Oracle Fast Application Notification (FAN). */

		DBD_ATTRIB_GET_IV(attr, "ora_init_mode",13, init_mode_sv, init_mode);

#if defined(USE_ITHREADS) || defined(MULTIPLICITY) || defined(USE_5005THREADS)
        init_mode |= OCI_THREADED;
#endif

		{
			size_t rsize = 0;
			/* Get CLIENT char and nchar charset id values */
			OCINlsEnvironmentVariableGet_log_stat(imp_dbh, &charsetid,(size_t) 0, OCI_NLS_CHARSET_ID, 0, &rsize ,status );
			if (status != OCI_SUCCESS) {
				oci_error(dbh, NULL, status,
					"OCINlsEnvironmentVariableGet(OCI_NLS_CHARSET_ID) Check NLS settings etc.");
				return 0;
			}

			OCINlsEnvironmentVariableGet_log_stat(imp_dbh, &ncharsetid,(size_t)  0, OCI_NLS_NCHARSET_ID, 0, &rsize ,status );
			if (status != OCI_SUCCESS) {
				oci_error(dbh, NULL, status,
					"OCINlsEnvironmentVariableGet(OCI_NLS_NCHARSET_ID) Check NLS settings etc.");
				return 0;
			}

			/*{
			After using OCIEnvNlsCreate() to create the environment handle,
			**the actual lengths and returned lengths of bind and define handles are
			always in number of bytes**. This applies to the following calls:

			* OCIBindByName()   * OCIBindByPos()	  * OCIBindDynamic()
			* OCIDefineByPos()  * OCIDefineDynamic()

			This function enables you to set charset and ncharset ids at
			environment creation time. [...]

			This function sets nonzero charset and ncharset as client side
			database and national character sets, replacing the ones specified
			by NLS_LANG and NLS_NCHAR. When charset and ncharset are 0, it
			behaves exactly the same as OCIEnvCreate(). Specifically, charset
			controls the encoding for metadata and data with implicit form
			attribute and ncharset controls the encoding for data with SQLCS_NCHAR
			form attribute.
			}*/

			OCIEnvNlsCreate_log_stat(imp_dbh, &imp_dbh->envhp, init_mode, 0, NULL, NULL, NULL, 0, NULL,
				charsetid, ncharsetid, status );

			if (status != OCI_SUCCESS) {
				oci_error(dbh, NULL, status,
					"OCIEnvNlsCreate. Check ORACLE_HOME (Linux) env var or PATH (Windows) and or NLS settings, permissions, etc.");
				return 0;
			}

			svp = DBD_ATTRIB_GET_SVP(attr, "ora_charset", 11);/*get the charset passed in by the user*/
			if (svp) {
				if (!SvPOK(*svp)) {
					croak("ora_charset is not a string");
				}

				new_charsetid = OCINlsCharSetNameToId(imp_dbh->envhp, (oratext*)SvPV_nolen(*svp));

				if (!new_charsetid) {
					croak("ora_charset value (%s) is not valid", SvPV_nolen(*svp));
				}

#ifdef ORA_OCI_112
				if (imp_dbh->using_drcp) {
					/* Store lookup from charset name to charset ID. */
					(void)hv_store_ent(imp_drh->charset_hv, *svp, newSViv(new_charsetid), 0);
				}
#endif
			}

			svp = DBD_ATTRIB_GET_SVP(attr, "ora_ncharset", 12); /*get the ncharset passed in by the user*/

			if (svp) {
				if (!SvPOK(*svp)) {
					croak("ora_ncharset is not a string");
				}

				new_ncharsetid = OCINlsCharSetNameToId(imp_dbh->envhp, (oratext*)SvPV_nolen(*svp));
				if (!new_ncharsetid) {
					croak("ora_ncharset value (%s) is not valid", SvPV_nolen(*svp));
				}

#ifdef ORA_OCI_112
				if (imp_dbh->using_drcp) {
					/* Store lookup from charset name to charset ID. */
					(void)hv_store_ent(imp_drh->charset_hv, *svp, newSViv(new_ncharsetid), 0);
				}
#endif
			}

			if (new_charsetid || new_ncharsetid) { /* reset the ENV with the new charset  from above*/
				if (new_charsetid) charsetid = new_charsetid;
				if (new_ncharsetid) ncharsetid = new_ncharsetid;
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				OCIEnvNlsCreate_log_stat(imp_dbh, &imp_dbh->envhp, init_mode, 0, NULL, NULL, NULL, 0, 0,
							charsetid, ncharsetid, status );
				if (status != OCI_SUCCESS) {
					oci_error(dbh, NULL, status,
						"OCIEnvNlsCreate. Check ORACLE_HOME (Linux) env var or PATH (Windows) and or NLS settings, permissions, etc");
					return 0;
				}
			}

#ifdef ORA_OCI_112
			if (!imp_dbh->using_drcp)
#endif
				if (!imp_drh->envhp)	/* cache first envhp info drh as future default */
					imp_drh->envhp = imp_dbh->envhp;

			/* update the hard-coded csid constants for unicode charsets */
			utf8_csid	   = OCINlsCharSetNameToId(imp_dbh->envhp, (void*)"UTF8");
			al32utf8_csid  = OCINlsCharSetNameToId(imp_dbh->envhp, (void*)"AL32UTF8");
			al16utf16_csid = OCINlsCharSetNameToId(imp_dbh->envhp, (void*)"AL16UTF16");
		}

#ifdef ORA_OCI_112
		if (imp_dbh->using_drcp) {
			/* Try looking up session pool again, in case ora_charsetid/ora_ncharsetid were used to specify previously used charset IDs from the NLS environment. */
			SV *key_sv = pool_key(imp_dbh, dbname, uid, pwd, charsetid, ncharsetid);

			if ((pool = pool_fetch(imp_drh, key_sv))) {
				imp_dbh->pool = pool;
				/* Free the current environment handle and replace it with the session pool's environment handle. */
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				imp_dbh->envhp = pool->envhp;
			}

			sv_free(key_sv);
		}
#endif
	}

	if (!imp_dbh->errhp) {
		OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_dbh->errhp, OCI_HTYPE_ERROR,  status);
	}

	OCIAttrGet_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, &charsetid, NULL,
			OCI_ATTR_ENV_CHARSET_ID, imp_dbh->errhp, status);

	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCIAttrGet OCI_ATTR_ENV_CHARSET_ID");
		return 0;
	}

	OCIAttrGet_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, &ncharsetid, NULL,
			OCI_ATTR_ENV_NCHARSET_ID, imp_dbh->errhp, status);

	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCIAttrGet OCI_ATTR_ENV_NCHARSET_ID");
		return 0;
	}

	/* At this point we have charsetid & ncharsetid
	*  note that it is possible for charsetid and ncharestid to
	*  be distinct if NLS_LANG and NLS_NCHAR are both used.
	*  BTW: NLS_NCHAR is set as follows: NSL_LANG=AL32UTF8
	*/

    if (DBIc_DBISTATE(imp_dbh)->debug >= 3 || dbd_verbose >= 3 ) {
		oratext  charsetname[OCI_NLS_MAXBUFSZ];
		oratext  ncharsetname[OCI_NLS_MAXBUFSZ];
		OCINlsCharSetIdToName(imp_dbh->envhp,charsetname, sizeof(charsetname),charsetid );
		OCINlsCharSetIdToName(imp_dbh->envhp,ncharsetname, sizeof(ncharsetname),ncharsetid );
		PerlIO_printf(
            DBIc_LOGPIO(imp_dbh),
            "	   charset id=%d, name=%s, ncharset id=%d, name=%s"
            " (csid: utf8=%d al32utf8=%d)\n",
            charsetid,charsetname, ncharsetid,ncharsetname, utf8_csid, al32utf8_csid);
#ifdef ORA_OCI_112
		if (imp_dbh->using_drcp)
			PerlIO_printf(DBIc_LOGPIO(imp_dbh)," Using DRCP Connection\n ");
#endif
	}

	if (!shared_dbh) {

#ifdef ORA_OCI_112

		if (imp_dbh->using_drcp) { /* connect using a DRCP */
			OCIAuthInfo *authp;
			ub4   purity = OCI_ATTR_PURITY_SELF;
			OraText *rettag;
			ub4 rettagl;
			/* pool Default values */
			if (!imp_dbh->pool_min )
				imp_dbh->pool_min = 0;
			if (!imp_dbh->pool_max )
				imp_dbh->pool_max = 40;
			if (!imp_dbh->pool_incr)
				imp_dbh->pool_incr = 1;
			if (!imp_dbh->pool_rlb)
				imp_dbh->pool_rlb = 0;

			if (!pool) {
				/* Create and cache new session pool struct. */
				SV *key_sv = pool_key(imp_dbh, dbname, uid, pwd, charsetid, ncharsetid);

				session_pool_t pool_data = {0};
				SV *pool_sv = newSVpvn((char*)&pool_data, sizeof(pool_data));
				HE *pool_he = hv_store_ent(imp_drh->pool_hv, key_sv, pool_sv, 0);
				imp_dbh->pool = pool = (session_pool_t*)SvPVX(HeVAL(pool_he));
				pool->envhp = imp_dbh->envhp;

				sv_free(key_sv);

				OCIHandleAlloc_ok(imp_dbh, pool->envhp, &pool->poolhp, OCI_HTYPE_SPOOL, status);
				OCIHandleAlloc_ok(imp_dbh, pool->envhp, &authp, OCI_HTYPE_AUTHINFO, status);
				/* Create an unshared error handle for use in pool creation and destruction. */
				OCIHandleAlloc_ok(imp_dbh, pool->envhp, &pool->errhp, OCI_HTYPE_ERROR,  status);

				OCIAttrSet_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO,
					imp_dbh->driver_name, (ub4)strlen(imp_dbh->driver_name),
					OCI_ATTR_DRIVER_NAME, pool->errhp, status);

				OCIAttrSet_log_stat(imp_dbh, pool->poolhp, OCI_HTYPE_SPOOL,
					authp, (ub4)0, OCI_ATTR_SPOOL_AUTH, pool->errhp, status);

				ora_parse_uid(imp_dbh, &uid, &pwd);

				OCISessionPoolCreate_log_stat(
					imp_dbh,
					pool->envhp,
					pool->errhp,
					pool->poolhp,
					&pool->pool_name,
					&pool->pool_namel,
					(OraText *) dbname,
					(ub4)strlen(dbname),
					imp_dbh->pool_min,
					imp_dbh->pool_max,
					imp_dbh->pool_incr,
					(OraText *) uid,
					(ub4)strlen(uid),
					(OraText *) pwd,
					(ub4)strlen(pwd),
					OCI_SPC_HOMOGENEOUS | (imp_dbh->pool_rlb ? 0 : OCI_SPC_NO_RLB),
					status);

				if (status != OCI_SUCCESS) {
					oci_error(dbh, pool->errhp, status, "OCISessionPoolCreate");
					OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);
					OCIHandleFree_log_stat(imp_dbh, pool->poolhp, OCI_HTYPE_SPOOL,status);
					OCIHandleFree_log_stat(imp_dbh, pool->errhp, OCI_HTYPE_ERROR,  status);
					/* Free the global error handle as well. */
					OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR, status);
					OCIHandleFree_log_stat(imp_dbh, pool->envhp, OCI_HTYPE_ENV, status);

					(void)hv_delete_ent(imp_drh->pool_hv, HeSVKEY(pool_he), 0, 0);
					return 0;
				}

				OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);
			}

			OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &authp, OCI_HTYPE_AUTHINFO, status);

			OCIAttrSet_log_stat(imp_dbh, authp, (ub4) OCI_HTYPE_AUTHINFO,
						&purity, (ub4) 0,(ub4) OCI_ATTR_PURITY, imp_dbh->errhp, status);

			if (imp_dbh->pool_class) /*pool_class may or may not be used */
				OCIAttrSet_log_stat(imp_dbh, authp, (ub4) OCI_HTYPE_AUTHINFO,
							(OraText *) imp_dbh->pool_class, (ub4) imp_dbh->pool_classl,
							(ub4) OCI_ATTR_CONNECTION_CLASS, imp_dbh->errhp, status);

			/* Use session tagging to get a server session initalized with correct charsets. */
			sprintf((char*)imp_dbh->session_tag, "csid=%d,ncsid=%d", charsetid, ncharsetid);

			OCISessionGet_log_stat(imp_dbh, imp_dbh->envhp, imp_dbh->errhp, &imp_dbh->svchp, authp,
						pool->pool_name, (ub4)strlen((char *)pool->pool_name),
						imp_dbh->session_tag, (ub4)strlen((char *)imp_dbh->session_tag), &rettag, &rettagl, &imp_dbh->session_tag_found, status);
			if (status != OCI_SUCCESS) {
				oci_error(dbh, imp_dbh->errhp, status, "OCISessionGet");
				OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);
				return 0;
			}

			/* Update number of active sessions in the pool. */
			++imp_dbh->pool->active_sessions;

			OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);

			/* Get server and session handles from service context handle, allocated by OCISessionGet. */
			OCIAttrGet_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, &imp_dbh->srvhp, NULL,
				OCI_ATTR_SERVER, imp_dbh->errhp, status);
			OCIAttrGet_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, &imp_dbh->seshp, NULL,
				OCI_ATTR_SESSION, imp_dbh->errhp, status);

			if (DBIc_DBISTATE(imp_dbh)->debug >= 4 || dbd_verbose >= 4 ) {
				PerlIO_printf(
				DBIc_LOGPIO(imp_dbh),
					"Using DRCP with session settings min=%d, max=%d, and increment=%d\n",
					imp_dbh->pool_min,
					imp_dbh->pool_max,
					imp_dbh->pool_incr);
				if (imp_dbh->pool_class)
					PerlIO_printf(
						DBIc_LOGPIO(imp_dbh),
						"with connection class=%s\n",imp_dbh->pool_class);
			}

		}
		else {
#endif /* ORA_OCI_112 */

			SV **sess_mode_type_sv;
			ub4  sess_mode_type = OCI_DEFAULT;
			ub4  cred_type;
			DBD_ATTRIB_GET_IV(attr, "ora_session_mode",16, sess_mode_type_sv, sess_mode_type);

			OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_dbh->srvhp, OCI_HTYPE_SERVER, status);
			OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_dbh->svchp, OCI_HTYPE_SVCCTX, status);
			OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_dbh->seshp, OCI_HTYPE_SESSION, status);
			OCIServerAttach_log_stat(imp_dbh, dbname,OCI_DEFAULT, status);
			if (status != OCI_SUCCESS) {
				oci_error(dbh, imp_dbh->errhp, status, "OCIServerAttach");
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->srvhp, OCI_HTYPE_SERVER, status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR, status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, status);
				if (imp_dbh->envhp != imp_drh->envhp) {
					OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				}
				return 0;
			}


			OCIAttrSet_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, imp_dbh->srvhp,
							(ub4) 0, OCI_ATTR_SERVER, imp_dbh->errhp, status);

			cred_type = ora_parse_uid(imp_dbh, &uid, &pwd);

#ifdef ORA_OCI_112
			OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp, (ub4)OCI_HTYPE_SESSION,
				imp_dbh->driver_name, (ub4)strlen(imp_dbh->driver_name),
				(ub4)OCI_ATTR_DRIVER_NAME, imp_dbh->errhp, status);
#endif

			OCISessionBegin_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, imp_dbh->seshp,cred_type, sess_mode_type, status);

			if (status == OCI_SUCCESS_WITH_INFO) {
				/* eg ORA-28011: the account will expire soon; change your password now */
				oci_error(dbh, imp_dbh->errhp, status, "OCISessionBegin");
				status = OCI_SUCCESS;
			}
			if (status != OCI_SUCCESS) {
				oci_error(dbh, imp_dbh->errhp, status, "OCISessionBegin");
				OCIServerDetach_log_stat(imp_dbh, imp_dbh->srvhp, imp_dbh->errhp, OCI_DEFAULT, status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->srvhp, OCI_HTYPE_SERVER, status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR,  status);
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, status);
				if (imp_dbh->envhp != imp_drh->envhp) {
					OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				}
				return 0;
			}

			OCIAttrSet_log_stat(imp_dbh, imp_dbh->svchp, (ub4) OCI_HTYPE_SVCCTX,
						imp_dbh->seshp, (ub4) 0,(ub4) OCI_ATTR_SESSION, imp_dbh->errhp, status);
#ifdef ORA_OCI_112
		}
#endif

	}

	DBIc_IMPSET_on(imp_dbh);	/* imp_dbh set up now			*/
	DBIc_ACTIVE_on(imp_dbh);	/* call disconnect before freeing	*/
	imp_dbh->ph_type = 1;	/* SQLT_CHR "(ORANET TYPE) character string" */
	imp_dbh->ph_csform = 0;	/* meaning auto (see dbd_rebind_ph)	*/

#if defined(USE_ITHREADS) && defined(PERL_MAGIC_shared_scalar)
	if (shared_dbh_ssv && !shared_dbh) {
	/* much of this could be replaced with a single sv_setpvn() */
		(void)SvUPGRADE(shared_dbh_priv_sv, SVt_PV);
		SvGROW(shared_dbh_priv_sv, sizeof(imp_dbh_t) + 1) ;
		SvCUR (shared_dbh_priv_sv) = sizeof(imp_dbh_t) ;
		imp_dbh->refcnt = 1 ;
		imp_dbh->shared_dbh_priv_sv = shared_dbh_priv_sv ;
		memcpy(SvPVX(shared_dbh_priv_sv) + DBH_DUP_OFF, ((char *)imp_dbh) + DBH_DUP_OFF, DBH_DUP_LEN) ;
		SvSETMAGIC(shared_dbh_priv_sv);
		imp_dbh->shared_dbh = (imp_dbh_t *)SvPVX(shared_dbh_ssv->sv);
	}
#endif

    /* set up TAF callback if wanted */

    if (imp_dbh->taf_function){
        if (enable_taf(dbh, imp_dbh) == 0) return 0;
	}

	return 1;
}


int
dbd_db_commit(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX;
	sword status;
	OCITransCommit_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, OCI_DEFAULT, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCITransCommit");
		return 0;
	}
	return 1;
}


int
dbd_st_cancel(SV *sth, imp_sth_t *imp_sth)
{
	dTHX;
	sword status;
	status = OCIBreak(imp_sth->svchp, imp_sth->errhp);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBreak");
		return 0;
	}

	 /* if we are using a scrolling cursor we should get rid of the
		cursor by fetching row 0 */
	if (imp_sth->exe_mode==OCI_STMT_SCROLLABLE_READONLY){
		OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp, 0,OCI_FETCH_NEXT,0,  status);
	}
	return 1;
}



int
dbd_db_rollback(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX;
	sword status;
	OCITransRollback_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, OCI_DEFAULT, status);
	if (status != OCI_SUCCESS) {
	oci_error(dbh, imp_dbh->errhp, status, "OCITransRollback");
	return 0;
	}
	return 1;
}

int dbd_st_bind_col(SV *sth, imp_sth_t *imp_sth, SV *col, SV *ref, IV type, SV *attribs) {
	dTHX;
	int field;

	if (!SvIOK(col)) {
		croak ("Invalid column number") ;
	}

	field = SvIV(col);

	if ((field < 1) || (field > DBIc_NUM_FIELDS(imp_sth))) {
		croak("cannot bind to non-existent field %d", field);
	}

    if (type != 0) {
        imp_sth->fbh[field-1].req_type = type;
    }
    if (attribs) {
        imp_sth->fbh[field-1].bind_flags = 0; /* default to none */
    }

#if DBIXS_REVISION >= 13590
	/* DBIXS 13590 added StrictlyTyped and DiscardString attributes */
	if (attribs) {
		HV *attr_hash;
		SV **attr;

		if (!SvROK(attribs)) {
			croak ("attributes is not a reference");
		}
		else if (SvTYPE(SvRV(attribs)) != SVt_PVHV) {
			croak ("attributes not a hash reference");
		}
		attr_hash = (HV *)SvRV(attribs);

		attr = hv_fetch(attr_hash, "StrictlyTyped", (U32)13, 0);
		if (attr && SvTRUE(*attr)) {
			imp_sth->fbh[field-1].bind_flags |= DBIstcf_STRICT;
		}

		attr = hv_fetch(attr_hash, "DiscardString", (U32)13, 0);
		if (attr && SvTRUE(*attr)) {
			imp_sth->fbh[field-1].bind_flags |= DBIstcf_DISCARD_STRING;
		}
	}
#endif  /* DBIXS_REVISION >= 13590 */
	return 1;
}

int
dbd_db_disconnect(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX;
	dTHR;
	int refcnt = 1 ;

#if defined(USE_ITHREADS) && defined(PERL_MAGIC_shared_scalar)
	if (DBIc_IMPSET(imp_dbh) && imp_dbh->shared_dbh) {
		SvLOCK (imp_dbh->shared_dbh_priv_sv) ;
		refcnt = imp_dbh -> shared_dbh -> refcnt ;
	}
#endif

	/* We assume that disconnect will always work	*/
	/* since most errors imply already disconnected.	*/
	DBIc_ACTIVE_off(imp_dbh);

	/* Oracle will commit on an orderly disconnect.	*/
	/* See DBI Driver.xst file for the DBI approach.	*/

	if (refcnt == 1 ) {
#ifdef ORA_OCI_112
		if (imp_dbh->using_drcp) {
			sword status;
			/* Release session, tagged for future retrieval. */
			OCISessionRelease_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp,
				imp_dbh->session_tag, (ub4)strlen((char *)imp_dbh->session_tag), imp_dbh->session_tag_found ? OCI_DEFAULT : OCI_SESSRLS_RETAG, status);
			if (status != OCI_SUCCESS) {
				oci_error(dbh, imp_dbh->errhp, status, "OCISessionRelease");
				return 0;
			}

			/* Update number of active sessions in the pool */
			--imp_dbh->pool->active_sessions;
		}
		else {
#endif
			sword s_se, s_sd;
			OCISessionEnd_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, imp_dbh->seshp,
			  OCI_DEFAULT, s_se);
			if (s_se) oci_error(dbh, imp_dbh->errhp, s_se, "OCISessionEnd");
			OCIServerDetach_log_stat(imp_dbh, imp_dbh->srvhp, imp_dbh->errhp, OCI_DEFAULT, s_sd);
			if (s_sd) oci_error(dbh, imp_dbh->errhp, s_sd, "OCIServerDetach");
			if (s_se != OCI_SUCCESS || s_sd != OCI_SUCCESS)
				return 0;
#ifdef ORA_OCI_112
		}
#endif
	}
	/* We don't free imp_dbh since a reference still exists	*/
	/* The DESTROY method is the only one to 'free' memory.	*/
	/* Note that statement objects may still exists for this dbh!	*/
	return 1;
}


void
dbd_db_destroy(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX ;
	int refcnt = 1 ;
	sword status;
	D_imp_drh_from_dbh;

#if defined(USE_ITHREADS) && defined(PERL_MAGIC_shared_scalar)
	if (DBIc_IMPSET(imp_dbh) && imp_dbh->shared_dbh) {
		SvLOCK (imp_dbh->shared_dbh_priv_sv) ;
		refcnt = imp_dbh -> shared_dbh -> refcnt-- ;
	}
#endif

	if (refcnt == 1) {
		if (DBIc_ACTIVE(imp_dbh))
			dbd_db_disconnect(dbh, imp_dbh);
		if (is_extproc)
			goto dbd_db_destroy_out;

		if (imp_dbh->taf_function){
            disable_taf(imp_dbh);
		}

        if (imp_dbh->taf_function) {
            SvREFCNT_dec(imp_dbh->taf_function);
            imp_dbh->taf_function = NULL;
        }
        if (imp_dbh->taf_ctx.dbh_ref) {
            SvREFCNT_dec(SvRV(imp_dbh->taf_ctx.dbh_ref));
            imp_dbh->taf_ctx.dbh_ref = NULL;
        }


#ifdef ORA_OCI_112
		if (imp_dbh->using_drcp) {
			OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR,  status);
		}
		else {
#endif
			OCIHandleFree_log_stat(imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,status);
			OCIHandleFree_log_stat(imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, status);
			OCIHandleFree_log_stat(imp_dbh, imp_dbh->srvhp, OCI_HTYPE_SERVER, status);
			OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR,  status);
			/* free session environment handle */
			if (imp_dbh->envhp != imp_drh->envhp) {
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				if ( status == OCI_SUCCESS ) {
					imp_dbh->envhp = NULL;
				}
			/* free global environment handle during destruction of last connection */
			} else if ( (imp_dbh->envhp == imp_drh->envhp) && (SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) ) {
				OCIHandleFree_log_stat(imp_dbh, imp_dbh->envhp, OCI_HTYPE_ENV, status);
				if ( status == OCI_SUCCESS ) {
					imp_dbh->envhp = NULL;
					imp_drh->envhp = NULL;
				}
			}
#ifdef ORA_OCI_112
		}
#endif
	}
	else {
		/* A new error handle is allocated on each new connect, so it is also freed when
		   refcnt > 1. Note that we cannot have a common free here, since it is an error
		   to free the environment handle before the error handle. */
		OCIHandleFree_log_stat(imp_dbh, imp_dbh->errhp, OCI_HTYPE_ERROR, status);
	}
dbd_db_destroy_out:
	DBIc_IMPSET_off(imp_dbh);
}


SV *
dbd_take_imp_data(SV *dbh, imp_xxh_t *imp_xxh, void* foo)
{
	dTHX;
	D_imp_dbh(dbh);
	D_imp_drh_from_dbh;

	/* By using ithread, handles will leak in dbd_dr_destroy() */
	imp_drh->leak_handles = 1;

	/* Indicate that SUPER::take_imp_data should be called. */
	return &PL_sv_no;
}


/* According to Oracle's documentation of OCISessionGet, attributes should not be changed
   on the server and session handles attached to OCISessionGet's service context handle.
   This would imply that dbd_db_STORE_attrib is wrong for session pooling, however
   it seems to work just fine... */
int
dbd_db_STORE_attrib(SV *dbh, imp_dbh_t *imp_dbh, SV *keysv, SV *valuesv)
{
	dTHX;
	STRLEN kl;
	STRLEN vl;
	sword status;
	char *key = SvPV(keysv,kl);
	int on = SvTRUE(valuesv);
	int cacheit = 1;

	if (kl==17 && strEQ(key, "ora_ncs_buff_mtpl") ) {
		ora_ncs_buff_mtpl = SvIV (valuesv);
	}
#ifdef ORA_OCI_112
	else if (kl==15 && strEQ(key, "ora_driver_name") ) {
		imp_dbh->driver_name = (char *) SvPV (valuesv, vl );
		OCIAttrSet_log_stat(
            imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION, imp_dbh->driver_name,
            (ub4)vl, OCI_ATTR_DRIVER_NAME, imp_dbh->errhp, status);
	}
	else if (kl==8 && strEQ(key, "ora_drcp") ) {
		imp_dbh->using_drcp = 1;
	}
	else if (kl==14 && strEQ(key, "ora_drcp_class") ) {
		STRLEN vl;
		imp_dbh->pool_class = (text *) SvPV (valuesv, vl );
		imp_dbh->pool_classl= (ub4) vl;
	}
	else if (kl==12 && strEQ(key, "ora_drcp_min") ) {
		imp_dbh->pool_min = SvIV (valuesv);
	}
	else if (kl==12 && strEQ(key, "ora_drcp_max") ) {
		imp_dbh->pool_max = SvIV (valuesv);
	}
	else if (kl==13 && strEQ(key, "ora_drcp_incr") ) {
		imp_dbh->pool_incr = SvIV (valuesv);
	}
	else if (kl==12 && strEQ(key, "ora_drcp_rlb") ) {
		imp_dbh->pool_rlb = SvIV (valuesv);
	}
#endif
	else if (kl==16 && strEQ(key, "ora_taf_function") ) {
        if (imp_dbh->taf_function)
            SvREFCNT_dec(imp_dbh->taf_function);
        imp_dbh->taf_function = newSVsv(valuesv);

        if (SvTRUE(valuesv)) {
            enable_taf(dbh, imp_dbh);
        } else {
            disable_taf(imp_dbh);
        }
	}
#ifdef OCI_ATTR_ACTION
	else if (kl==10 && strEQ(key, "ora_action") ) {
		imp_dbh->action = (char *) SvPV (valuesv, vl );
		imp_dbh->actionl= (ub4) vl;
		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp,OCI_HTYPE_SESSION, imp_dbh->action,imp_dbh->actionl,OCI_ATTR_ACTION,imp_dbh->errhp, status);

	}
#endif
	else if (kl==21 && strEQ(key, "ora_client_identifier") ) {
		imp_dbh->client_identifier = (char *) SvPV (valuesv, vl );
		imp_dbh->client_identifierl= (ub4) vl;
		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp,OCI_HTYPE_SESSION, imp_dbh->client_identifier,imp_dbh->client_identifierl,OCI_ATTR_CLIENT_IDENTIFIER,imp_dbh->errhp, status);

	}
#ifdef OCI_ATTR_CLIENT_INFO
    else if (kl==15 && strEQ(key, "ora_client_info") ) {
		imp_dbh->client_info = (char *) SvPV (valuesv, vl );
		imp_dbh->client_infol= (ub4) vl;
		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp,OCI_HTYPE_SESSION, imp_dbh->client_info,imp_dbh->client_infol,OCI_ATTR_CLIENT_INFO,imp_dbh->errhp, status);
	}
#endif
#ifdef OCI_ATTR_MODULE
	else if (kl==15 && strEQ(key, "ora_module_name") ) {
		imp_dbh->module_name = (char *) SvPV (valuesv, vl );
		imp_dbh->module_namel= (ub4) vl;
		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp,OCI_HTYPE_SESSION, imp_dbh->module_name,imp_dbh->module_namel,OCI_ATTR_MODULE,imp_dbh->errhp, status);

	}
#endif
	else if (kl==20 && strEQ(key, "ora_oci_success_warn") ) {
		oci_warn = SvIV (valuesv);
	}
	else if (kl==11 && strEQ(key, "ora_objects")) {
		ora_objects = SvIV (valuesv);
	}
	else if (kl==11 && (strEQ(key, "ora_verbose") || strEQ(key, "dbd_verbose"))) {
		dbd_verbose = SvIV (valuesv);
	}
	else if (kl==10 && strEQ(key, "AutoCommit")) {
		DBIc_set(imp_dbh,DBIcf_AutoCommit, on);
	}
	else if (kl==12 && strEQ(key, "RowCacheSize")) {
		imp_dbh->RowCacheSize = SvIV(valuesv);
	}
	else if (kl==22 && strEQ(key, "ora_max_nested_cursors")) {
		imp_dbh->max_nested_cursors = SvIV(valuesv);
	}
	else if (kl==20 && strEQ(key, "ora_array_chunk_size")) {
			imp_dbh->array_chunk_size = SvIV(valuesv);
	}
	else if (kl==11 && strEQ(key, "ora_ph_type")) {
		if (SvIV(valuesv)!=1 && SvIV(valuesv)!=5 && SvIV(valuesv)!=96 && SvIV(valuesv)!=97)
			warn("ora_ph_type must be 1 (VARCHAR2), 5 (STRING), 96 (CHAR), or 97 (CHARZ)");
		else
			imp_dbh->ph_type = SvIV(valuesv);
		 }

	else if (kl==13 && strEQ(key, "ora_ph_csform")) {
		if (SvIV(valuesv)!=SQLCS_IMPLICIT && SvIV(valuesv)!=SQLCS_NCHAR)
			warn("ora_ph_csform must be 1 (SQLCS_IMPLICIT) or 2 (SQLCS_NCHAR)");
		else
			imp_dbh->ph_csform = (ub1)SvIV(valuesv);
		}
	else
	{
		return FALSE;
	}

	if (cacheit) /* cache value for later DBI 'quick' fetch? */
		(void)hv_store((HV*)SvRV(dbh), key, kl, newSVsv(valuesv), 0);

	return TRUE;
}


SV *
dbd_db_FETCH_attrib(SV *dbh, imp_dbh_t *imp_dbh, SV *keysv)
{
	dTHX;
	STRLEN kl;
	char *key = SvPV(keysv,kl);
	SV *retsv = Nullsv;
	/* Default to caching results for DBI dispatch quick_FETCH	*/
	int cacheit = FALSE;

	/* AutoCommit FETCH via DBI */

	if (kl==18 && strEQ(key, "ora_ncs_buff_mtpl") ) {
		retsv = newSViv (ora_ncs_buff_mtpl);
	}
#ifdef ORA_OCI_112
	else if (kl==15 && strEQ(key, "ora_driver_name") ) {
		retsv = newSVpv((char *)imp_dbh->driver_name,0);
	}
	else if (kl==8 && strEQ(key, "ora_drcp") ) {
		retsv = newSViv(imp_dbh->using_drcp);
	}
	else if (kl==14 && strEQ(key, "ora_drcp_class") ) {
		retsv = newSVpv((char *)imp_dbh->pool_class, 0);
	}
	else if (kl==12 && strEQ(key, "ora_drcp_min") ) {
		retsv = newSViv(imp_dbh->pool_min);
	}
	else if (kl==12 && strEQ(key, "ora_drcp_max") ) {
		retsv = newSViv(imp_dbh->pool_max);
	}
	else if (kl==13 && strEQ(key, "ora_drcp_incr") ) {
		retsv = newSViv(imp_dbh->pool_incr);
	}
	else if (kl==12 && strEQ(key, "ora_drcp_rlb") ) {
		retsv = newSViv(imp_dbh->pool_rlb);
	}
#endif
	else if (kl==16 && strEQ(key, "ora_taf_function") ) {
        if (imp_dbh->taf_function) {
            retsv = newSVsv(imp_dbh->taf_function);
        }
	}
#ifdef OCI_ATTR_ACTION
	else if (kl==10 && strEQ(key, "ora_action")) {
		retsv =  newSVpv((char *)imp_dbh->action,0);
	}
#endif
    else if (kl==21 && strEQ(key, "ora_client_identifier")) {
		retsv =  newSVpv((char *)imp_dbh->client_identifier,0);
	}
	else if (kl==15 && strEQ(key, "ora_client_info")) {
		retsv =  newSVpv((char *)imp_dbh->client_info,0);
	}
	else if (kl==15 && strEQ(key, "ora_module_name")) {
		retsv =  newSVpv((char *)imp_dbh->module_name,0);
	}
	else if (kl==20 && strEQ(key, "ora_oci_success_warn")) {
		retsv = newSViv (oci_warn);
	}
	else if (kl==11 && strEQ(key, "ora_objects")) {
		retsv = newSViv (ora_objects);
	}
	else if (kl==11 && (strEQ(key, "ora_verbose") || strEQ(key, "dbd_verbose"))) {
		retsv = newSViv (dbd_verbose);
	}
	else if (kl==10 && strEQ(key, "AutoCommit")) {
		retsv = boolSV(DBIc_has(imp_dbh,DBIcf_AutoCommit));
	}
	else if (kl==12 && strEQ(key, "RowCacheSize")) {
		retsv = newSViv(imp_dbh->RowCacheSize);
	}
	else if (kl==11 && strEQ(key, "RowsInCache")) {
			retsv = newSViv(imp_dbh->RowsInCache);
	}
	else if (kl==22 && strEQ(key, "ora_max_nested_cursors")) {
		retsv = newSViv(imp_dbh->max_nested_cursors);
	}
	else if (kl==11 && strEQ(key, "ora_ph_type")) {
		retsv = newSViv(imp_dbh->ph_type);
	}
	else if (kl==13 && strEQ(key, "ora_ph_csform")) {
		retsv = newSViv(imp_dbh->ph_csform);
	}
	else if (kl==22 && strEQ(key, "ora_parse_error_offset")) {
		retsv = newSViv(imp_dbh->parse_error_offset);
	}
	if (!retsv)
		return Nullsv;
	if (cacheit) {	/* cache for next time (via DBI quick_FETCH)	*/
		SV **svp = hv_fetch((HV*)SvRV(dbh), key, kl, 1);
		sv_free(*svp);
		*svp = retsv;
		(void)SvREFCNT_inc(retsv);	/* so sv_2mortal won't free it	*/
	}

	if (retsv == &PL_sv_yes || retsv == &PL_sv_no)
		return retsv; /* no need to mortalize yes or no */

	return sv_2mortal(retsv);
}



/* ================================================================== */

#define MAX_OCISTRING_LEN 32766

SV *
createxmlfromstring(SV *sth, imp_sth_t *imp_sth, SV *source){

	dTHX;
	dTHR;
	OCIXMLType *xml = NULL;
	STRLEN len;
	ub4 buflen;
	sword status;
	ub1 src_type;
	dvoid* src_ptr = NULL;
	D_imp_dbh_from_sth;
	SV* sv_dest;
	dvoid *bufp;
	ub1 csform;
	ub2 csid;
	csid 	= 0;
	csform 	= SQLCS_IMPLICIT;
	len 	= SvLEN(source);
	bufp 	= SvPV(source, len);

	if (DBIc_DBISTATE(imp_sth)->debug >=3 || dbd_verbose >= 3 )
        PerlIO_printf(DBIc_LOGPIO(imp_sth), " creating xml from string that is %lu long\n",(unsigned long)len);
	if(len > MAX_OCISTRING_LEN) {
		src_type = OCI_XMLTYPE_CREATE_CLOB;

		if (DBIc_DBISTATE(imp_sth)->debug >=5 || dbd_verbose >= 5 )
			PerlIO_printf(DBIc_LOGPIO(imp_sth),
                          " use a temp lob locator for large xml \n");

		OCIDescriptorAlloc_ok(imp_dbh, imp_dbh->envhp, &src_ptr, OCI_DTYPE_LOB);

		OCILobCreateTemporary_log_stat(imp_dbh, imp_dbh->svchp, imp_sth->errhp,
					 (OCILobLocator *) src_ptr, (ub2) OCI_DEFAULT,
					 (ub1) OCI_DEFAULT, OCI_TEMP_CLOB, FALSE, OCI_DURATION_SESSION, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobCreateTemporary");
		}
		csid = (SvUTF8(source) && !CS_IS_UTF8(csid)) ? utf8_csid : CSFORM_IMPLIED_CSID(csform);
		buflen = len;
		OCILobWriteAppend_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, src_ptr,
						&buflen, bufp, (ub4)len, OCI_ONE_PIECE,
						NULL, NULL,
						csid, csform, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobWriteAppend");
		}

	} else {
		src_type = OCI_XMLTYPE_CREATE_OCISTRING;
		if (DBIc_DBISTATE(imp_sth)->debug >=5 || dbd_verbose >= 5 )
			PerlIO_printf(DBIc_LOGPIO(imp_sth),
                          " use a OCIStringAssignText for small xml \n");
		OCIStringAssignText(imp_dbh->envhp,
					imp_dbh->errhp,
					bufp,
					(ub2) (ub4)len,
					(OCIString **) &src_ptr);
	}



	OCIXMLTypeCreateFromSrc_log_stat(imp_dbh,
                                     imp_dbh->svchp,
                                     imp_dbh->errhp,
                                     (OCIDuration)OCI_DURATION_CALLOUT,
                                     (ub1)src_type,
                                     (dvoid *)src_ptr,
                                     (sb4)OCI_IND_NOTNULL,
                                     &xml,
                                     status);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIXMLTypeCreateFromSrc");
	}

/* free temporary resources */
	if ( src_type == OCI_XMLTYPE_CREATE_CLOB ) {
		OCILobFreeTemporary(imp_dbh->svchp, imp_dbh->errhp,
					(OCILobLocator*) src_ptr);

		OCIDescriptorFree_log(imp_dbh, (dvoid *) src_ptr, (ub4) OCI_DTYPE_LOB);
	}


	sv_dest = newSViv(0);
	sv_setref_pv(sv_dest, "OCIXMLTypePtr", xml);
	return sv_dest;

}


void
dbd_preparse(imp_sth_t *imp_sth, char *statement)
{
dTHX;
D_imp_dbh_from_sth;
char in_literal = '\0';
char in_comment = '\0';
char *src, *start, *dest;
phs_t phs_tpl;
SV *phs_sv;
int idx=0;
char *style="", *laststyle=Nullch;
STRLEN namelen;
phs_t *phs;
	/* allocate room for copy of statement with spare capacity	*/
	/* for editing '?' or ':1' into ':p1' so we can use obndrv.	*/
	/* XXX should use SV and append to it */
	Newz(0,imp_sth->statement,strlen(statement) * 10,char);

	/* initialise phs ready to be cloned per placeholder	*/
	memset(&phs_tpl, 0, sizeof(phs_tpl));
	phs_tpl.imp_sth = imp_sth;
	phs_tpl.ftype  = imp_dbh->ph_type;
	phs_tpl.csform = imp_dbh->ph_csform;
	phs_tpl.sv = &PL_sv_undef;

	src  = statement;
	dest = imp_sth->statement;
	while(*src) {

		if (in_comment) {
		/* 981028-jdl on mocha.  Adding all code which deals with		   */
		/*  in_comment variable (its declaration plus 2 code blocks).	   */
		/*  Text appearing within comments should be scanned for neither	*/
		/*  placeholders nor for single quotes (which toggle the in_literal */
		/*  boolean).  Comments like "3:00" demonstrate the former problem, */
		/*  and contractions like "don't" demonstrate the latter problem.   */
		/* The comment style is stored in in_comment; each style is */
		/* terminated in a different way.						  */
			if (in_comment == '-' && *src == '\n') {
				in_comment = '\0';
			}
			else if (in_comment == '/' && *src == '*' && *(src+1) == '/') {
				*dest++ = *src++; /* avoids asterisk-slash-asterisk issues */
				in_comment = '\0';
			}
			*dest++ = *src++;
			continue;
		}

		if (in_literal) {
			if (*src == in_literal)
				in_literal = '\0';
			*dest++ = *src++;
			continue;
		}

		/* Look for comments: '-- oracle-style' or C-style	*/
		if ((*src == '-' && *(src+1) == '-') ||
			(*src == '/' && *(src+1) == '*'))
		{
			in_comment = *src;
			/* We know *src & the next char are to be copied, so do */
			/*  it.  In the case of C-style comments, it happens to */
			/*  help us avoid slash-asterisk-slash oddities.		*/
			*dest++ = *src++;
			*dest++ = *src++;
			continue;
		}

		if (*src != ':' && *src != '?') {

			if (*src == '\'' || *src == '"')
				in_literal = *src;

			*dest++ = *src++;
			continue;
		}

		/* only here for : or ? outside of a comment or literal	*/

		start = dest;			/* save name inc colon	*/
		*dest++ = *src++;
		if (*start == '?') {		/* X/Open standard	*/
			sprintf(start,":p%d", ++idx); /* '?' -> ':p1' (etc)	*/
			dest = start+strlen(start);
			style = "?";

		}
		else if (isDIGIT(*src)) {	/* ':1'		*/
			idx = atoi(src);
			*dest++ = 'p';		/* ':1'->':p1'	*/
			if (idx <= 0)
				croak("Placeholder :%d invalid, placeholders must be >= 1", idx);

			while(isDIGIT(*src))
				*dest++ = *src++;
			style = ":1";

		}
		else if (isALNUM(*src)) {	/* ':foo'	*/
			while(isALNUM(*src))	/* includes '_'	*/
				*dest++ = toLOWER(*src), src++;
			style = ":foo";

		} else {			/* perhaps ':=' PL/SQL construct */
			/* if (src == ':') *dest++ = *src++; XXX? move past '::'? */
			continue;
		}

		*dest = '\0';			/* handy for debugging	*/
		namelen = (dest-start);
		if (laststyle && style != laststyle)
			croak("Can't mix placeholder styles (%s/%s)",style,laststyle);
		laststyle = style;
		if (imp_sth->all_params_hv == NULL)
			imp_sth->all_params_hv = newHV();
		/* allocate and copy enough for phs_tpl */
		phs_sv = newSVpvn((char*)&phs_tpl, sizeof(phs_tpl));
		(void)hv_store(imp_sth->all_params_hv, start, namelen, phs_sv, 0);
		/* allocate extra room for the name (returns the PV) */
		phs = (phs_t*)(void*)SvGROW(phs_sv, sizeof(phs_tpl)+namelen+1);
		phs->idx = idx-1;          /* Will be 0 for :1, -1 for :foo. */
		/* tell the SV the full length */
		SvCUR_set(phs_sv, sizeof(phs_tpl)+namelen);
		/* copy the name */
		strcpy(phs->name, start);
	}
	*dest = '\0';
	if (imp_sth->all_params_hv) {
		DBIc_NUM_PARAMS(imp_sth) = (int)HvKEYS(imp_sth->all_params_hv);
		if (DBIc_DBISTATE(imp_sth)->debug >= 2 || dbd_verbose >= 3 )
			PerlIO_printf(DBIc_LOGPIO(imp_sth),
                          "	dbd_preparse scanned %d distinct placeholders\n",
                          (int)DBIc_NUM_PARAMS(imp_sth));
	}
}


static int
ora_sql_type(imp_sth_t *imp_sth, char *name, int sql_type)
{
	/* XXX should detect DBI reserved standard type range here */

	switch (sql_type) {
	case SQL_NUMERIC:
	case SQL_DECIMAL:
	case SQL_INTEGER:
	case SQL_BIGINT:
	case SQL_TINYINT:
	case SQL_SMALLINT:
	case SQL_FLOAT:
	case SQL_REAL:
	case SQL_DOUBLE:
	case SQL_VARCHAR:
	return 1;	/* Oracle VARCHAR2	*/

	case SQL_CHAR:
	return 96;	/* Oracle CHAR		*/

	case SQL_BINARY:
	case SQL_VARBINARY:
	return 23;	/* Oracle RAW		*/

	case SQL_LONGVARBINARY:
	return 24;	/* Oracle LONG RAW	*/

	case SQL_LONGVARCHAR:
	return 8;	/* Oracle LONG		*/

	case SQL_UDT:
 		return 108;	 /* Oracle NTY		   */

	case SQL_CLOB:
	return 112;	/* Oracle CLOB		*/

	case SQL_BLOB:
	return 113;	/* Oracle BLOB		*/

	case SQL_DATE:
	case SQL_TIME:
	case SQL_TIMESTAMP:
	default:
	if (imp_sth && DBIc_WARN(imp_sth) && name)
		warn("SQL type %d for '%s' is not fully supported, bound as SQL_VARCHAR instead",
		sql_type, name);
	return ora_sql_type(imp_sth, name, SQL_VARCHAR);
	}
}



/* ############### Array bind ######################################### */
/* Added by Alexander V Alekseev. alex@alemate.ru					   */
/*
 *
 * Realloc temporary array buffer to match required number of entries
 * and buffer size.
 *
 * Return value: croaks on error. false (=0 ) on success.
 * */
int ora_realloc_phs_array(phs_t *phs,int newentries, int newbufsize){

	dTHX;
	dTHR;
	int i; /* Loop variable */
	unsigned short *newal;

	if( newbufsize < 0 ){
	newbufsize=0;
	}
	if( newentries > phs->array_numallocated ){
		OCIInd *newind=(OCIInd *)realloc(phs->array_indicators,newentries*sizeof(OCIInd) );
	if( newind ){
		phs->array_indicators=newind;
		/* Init all indicators to NULL values. */
		for( i=phs->array_numallocated; i < newentries ; i++ ){
		newind[i]=1;
		}
	}else{
		croak("Not enough memory to allocate %d OCI indicators.",newentries);
	}
	newal=(unsigned short *)realloc(phs->array_lengths,	newentries*sizeof(unsigned short));
	if( newal ){
		phs->array_lengths=newal;
		/* Init all new lengths to zero */
		if( newentries > phs->array_numallocated ){
			memset(
				&(newal[phs->array_numallocated]),
				0,
				(newentries-(phs->array_numallocated))*sizeof(unsigned short)
			  );
		}
	}else{
		croak("Not enough memory to allocate %d entries in OCI array of lengths.",newentries);
	}
	phs->array_numallocated=newentries;
	}
	if( phs->array_buflen < newbufsize ){
	char * newbuf=(char *)realloc( phs->array_buf, (unsigned) newbufsize );
	if( newbuf ){
		phs->array_buf=newbuf;
	}else{
		croak("Not enough memory to allocate OCI array buffer of %d bytes.",newbufsize);
	}
	phs->array_buflen=newbufsize;
	}
	return 0;
}
/* bind of SYS.DBMS_SQL.VARCHAR2_TABLE */
int
dbd_rebind_ph_varchar2_table(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	/*D_imp_dbh_from_sth ;*/
	sword status;
	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	AV *arr;
	ub1 csform;
	ub2 csid;
	int flag_data_is_utf8=0;
	int need_allocate_rows;
	int buflen;
	int numarrayentries;
	if( ( ! SvROK(phs->sv) )  || (SvTYPE(SvRV(phs->sv))!=SVt_PVAV) ) { /* Allow only array binds */
	croak("dbd_rebind_ph_varchar2_table(): bad bind variable. ARRAY reference required, but got %s for '%s'.",
			neatsvpv(phs->sv,0), phs->name);
	}
	arr=(AV*)(SvRV(phs->sv));

	if (trace_level >= 2 || dbd_verbose >= 3 ){
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
                      "dbd_rebind_ph_varchar2_table(): array_numstruct=%d\n",
		  phs->array_numstruct);
	}
	/* If no number of entries to bind specified,
	 * set phs->array_numstruct to the scalar(@array) bound.
	 */
	/* av_len() returns last array index, or -1 is array is empty */
	numarrayentries=av_len( arr );

	if( numarrayentries >= 0 ){
		phs->array_numstruct = numarrayentries+1;
		if (trace_level >= 2 || dbd_verbose >= 3 ){
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_varchar2_table(): array_numstruct=%d (calculated) \n",
                phs->array_numstruct);
		}
	}
	/* Fix charset */
	csform = phs->csform;
	if (trace_level >= 2 || dbd_verbose >= 3 ){
        PerlIO_printf(DBIc_LOGPIO(imp_sth),
                      "dbd_rebind_ph_varchar2_table(): original csform=%d\n",
                      (int)csform);
	}
	/* Calculate each bound structure maxlen.
	* If maxlen<=0, let maxlen=MAX ( length($$_) each @array );
	*
	* Charset calculation is done inside this loop either.
	*/
	{
	unsigned int maxlen=0;
	int i;

	for(i=0;i<av_len(arr)+1;i++){
		SV *item;
		item=*(av_fetch(arr,i,0));
		if( item ){
		if( phs->maxlen <=0 ){ /* Analyze maxlength only if not forced */
			STRLEN length=0;
			if (!SvPOK(item)) {	 /* normalizations for special cases	 */
				if (SvOK(item)) {	/* ie a number, convert to string ASAP  */
					if (!(SvROK(item) && phs->is_inout)){
						sv_2pv(item, &length);
					}
				} else { /* ensure we're at least an SVt_PV (so SvPVX etc work)	 */
					(void)SvUPGRADE(item, SVt_PV);
				}
			}
			if( length == 0 ){
				length=SvCUR(item);
			}
			if( length+1 > maxlen ){
			maxlen=length+1;
			}
			if (trace_level >= 3 || dbd_verbose >= 3 ){
                PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "dbd_rebind_ph_varchar2_table(): length(array[%d])=%d\n",
                    i,(int)length);
			}
		}
		if(SvUTF8(item) ){
			flag_data_is_utf8=1;
			if (trace_level >= 3 || dbd_verbose >= 3 ){
                PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "dbd_rebind_ph_varchar2_table(): is_utf8(array[%d])=true\n", i);
			}
			if (csform != SQLCS_NCHAR) {
			/* try to default csform to avoid translation through non-unicode */
			if (CSFORM_IMPLIES_UTF8(SQLCS_NCHAR))		/* prefer NCHAR */
				csform = SQLCS_NCHAR;
			else if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT))
				csform = SQLCS_IMPLICIT;
			/* else leave csform == 0 */
			if (trace_level  || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "dbd_rebind_ph_varchar2_table(): rebinding %s with UTF8 value %s",
                    phs->name,
					(csform == SQLCS_NCHAR)	? "so setting csform=SQLCS_IMPLICIT" :
					(csform == SQLCS_IMPLICIT) ? "so setting csform=SQLCS_NCHAR" :
					"but neither CHAR nor NCHAR are unicode\n");
			}
		}else{
			if (trace_level >= 3 || dbd_verbose >= 3 ){
                PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "dbd_rebind_ph_varchar2_table(): is_utf8(array[%d])=false\n", i);
			}
		}
		}
	}
	if( phs->maxlen <=0 ){
		phs->maxlen=maxlen;
		if (trace_level >= 2 || dbd_verbose >= 3 ){
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_varchar2_table(): phs->maxlen calculated  =%ld\n",
                (long)maxlen);
		}
	} else{
		if (trace_level >= 2 || dbd_verbose >= 3 ){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_varchar2_table(): phs->maxlen forsed =%ld\n",
                (long)maxlen);
		}
	}
	}
	/* Do not allow string bind longer than max VARCHAR2=4000+1 */
	if( phs->maxlen > 4001 ){
	phs->maxlen=4001;
	}

	if( phs->array_numstruct == 0 ){
	/* Oracle doesn't allow NULL buffers even for empty tables. Don't know why. */
		phs->array_numstruct=1;
	}
	if( phs->ora_maxarray_numentries== 0 ){
	/* Zero means "use current array length". */
		phs->ora_maxarray_numentries=phs->array_numstruct;
	}

	need_allocate_rows=phs->ora_maxarray_numentries;

	if( need_allocate_rows< phs->array_numstruct ){
		need_allocate_rows=phs->array_numstruct;
	}
	buflen=need_allocate_rows* phs->maxlen; /* We need buffer for at least ora_maxarray_numentries entries */
	/* Upgrade array buffer to new length */
	if( ora_realloc_phs_array(phs,need_allocate_rows,buflen) ){
        croak("Unable to bind %s - %d structures by %d bytes requires too much memory.",
              phs->name, need_allocate_rows, buflen );
	}else{
        if (trace_level >= 2 || dbd_verbose >= 3 ){
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_varchar2_table(): ora_realloc_phs_array(,"
                "need_allocate_rows=%d,buflen=%d) succeeded.\n",
                need_allocate_rows,buflen);
        }
	}
	/* If maximum allowed bind numentries is less than allowed,
	 * do not bind full array
	 */
	if( phs->array_numstruct > phs->ora_maxarray_numentries ){
		phs->array_numstruct = phs->ora_maxarray_numentries;
	}
	/* Fill array buffer with string data */

	{
        int i; /* Not to require C99 mode */
        for(i=0;i<av_len(arr)+1;i++){
            SV *item;
            item=*(av_fetch(arr,i,0));
            if( item ){
                STRLEN itemlen;
                char *str=SvPV(item, itemlen);
                if( str && (itemlen>0) ){
			/* Limit string length to maxlen. FIXME: This may corrupt UTF-8 data. */
                    if( itemlen > (unsigned int) phs->maxlen-1 ){
                        itemlen=phs->maxlen-1;
                    }
                    memcpy( phs->array_buf+phs->maxlen*i,
                            str,
                            itemlen);
                    /* Set last byte to zero */
                    phs->array_buf[ phs->maxlen*i + itemlen ]=0;
                    phs->array_indicators[i]=0;
                    phs->array_lengths[i]=itemlen+1; /* Zero byte */
                    if (trace_level >= 3 || dbd_verbose >= 3 ){
                        PerlIO_printf(
                            DBIc_LOGPIO(imp_sth),
                            "dbd_rebind_ph_varchar2_table(): "
                            "Copying length=%lu array[%d]='%s'.\n",
                            (unsigned long)itemlen,i,str);
                    }
                }else{
                    /* Mark NULL */
                    phs->array_indicators[i]=1;
                    if (trace_level >= 3 || dbd_verbose >= 3 ){
                        PerlIO_printf(
                            DBIc_LOGPIO(imp_sth),
                            "dbd_rebind_ph_varchar2_table(): "
                            "Copying length=%lu array[%d]=NULL (length==0 or ! str) .\n",
                            (unsigned long)itemlen,i);
                    }
                }
            }else{
                /* Mark NULL */
                phs->array_indicators[i]=1;
                if (trace_level >= 3 || dbd_verbose >= 3 ) {
                    PerlIO_printf(
                        DBIc_LOGPIO(imp_sth),
                        "dbd_rebind_ph_varchar2_table(): "
                        "Copying length=? array[%d]=NULL av_fetch failed.\n", i);
                }
            }
        }
	}
	/* Do actual bind */
	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
		(text*)phs->name, (sb4)strlen(phs->name),
		phs->array_buf,
		phs->maxlen,
		(ub2)SQLT_STR, phs->array_indicators,
		phs->array_lengths,	/* ub2 *alen_ptr not needed with OCIBindDynamic */
		NULL,
		(ub4)phs->ora_maxarray_numentries, /* max elements that can fit in allocated array	*/
		(ub4 *)&(phs->array_numstruct),	/* (ptr to) current number of elements in array	*/
		OCI_DEFAULT,				/* OCI_DATA_AT_EXEC (bind with callbacks) or OCI_DEFAULT  */
		status
	);
	if (status != OCI_SUCCESS) {
	oci_error(sth, imp_sth->errhp, status, "OCIBindByName");
	return 0;
	}
	OCIBindArrayOfStruct_log_stat(imp_sth, phs->bndhp, imp_sth->errhp,
		(unsigned)phs->maxlen,			/* Skip parameter for the next data value */
		(unsigned)sizeof (OCIInd),		/* Skip parameter for the next indicator value */
		(unsigned)sizeof(unsigned short), /* Skip parameter for the next actual length value */
		0,					  			/* Skip parameter for the next column-level error code */
		status);
	if (status != OCI_SUCCESS) {
	oci_error(sth, imp_sth->errhp, status, "OCIBindArrayOfStruct");
	return 0;
	}
	/* Fixup charset */
	if (csform) {
		/* set OCI_ATTR_CHARSET_FORM before we get the default OCI_ATTR_CHARSET_ID */
        OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
		&csform, (ub4) 0, (ub4) OCI_ATTR_CHARSET_FORM, imp_sth->errhp, status);
	if ( status != OCI_SUCCESS ) {
		oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_FORM)"));
		return 0;
	}
	}

	if (!phs->csid_orig) {	/* get the default csid Oracle would use */
		OCIAttrGet_log_stat(imp_sth, phs->bndhp, OCI_HTYPE_BIND, &phs->csid_orig, NULL,
			OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
	}

	/* if app has specified a csid then use that, else use default */
	csid = (phs->csid) ? phs->csid : phs->csid_orig;

	/* if data is utf8 but charset isn't then switch to utf8 csid */
	if ( flag_data_is_utf8 && !CS_IS_UTF8(csid))
		csid = utf8_csid; /* not al32utf8_csid here on purpose */

	if (trace_level >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph_varchar2_table(): bind %s <== %s "
			"(%s, %s, csid %d->%d->%d, ftype %d, csform %d (%s)->%d (%s), maxlen %lu, maxdata_size %lu)\n",
			phs->name, neatsvpv(phs->sv,0),
			(phs->is_inout) ? "inout" : "in",
			flag_data_is_utf8 ? "is-utf8" : "not-utf8",
			phs->csid_orig, phs->csid, csid,
			phs->ftype, phs->csform,oci_csform_name(phs->csform), csform,oci_csform_name(csform),
			(unsigned long)phs->maxlen, (unsigned long)phs->maxdata_size);


	if (csid) {
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
			&csid, (ub4) 0, (ub4) OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_ID)"));
		return 0;
		}
	}

	if (phs->maxdata_size) {
        OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4)OCI_HTYPE_BIND,
		phs->array_buf, (ub4)phs->array_buflen, (ub4)OCI_ATTR_MAXDATA_SIZE, imp_sth->errhp, status);
	if ( status != OCI_SUCCESS ) {
		oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_MAXDATA_SIZE)"));
		return 0;
	}
	}

	return 2;
}


/* Copy array data from array buffer into perl array */
/* Returns false on error, true on success */
int dbd_phs_varchar_table_posy_exe(imp_sth_t *imp_sth, phs_t *phs){
	dTHX;

	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	AV *arr;

	if( ( ! SvROK(phs->sv) )  || (SvTYPE(SvRV(phs->sv))!=SVt_PVAV) ) { /* Allow only array binds */
	croak("dbd_phs_varchar_table_posy_exe(): bad bind variable. ARRAY reference required, but got %s for '%s'.",
			neatsvpv(phs->sv,0), phs->name);
	}
	if (trace_level >= 1 || dbd_verbose >= 3 ){
        PerlIO_printf(DBIc_LOGPIO(imp_sth),
		"dbd_phs_varchar_table_posy_exe(): Called for '%s' : array_numstruct=%d, maxlen=%ld \n",
		phs->name,
		phs->array_numstruct,
		(long)phs->maxlen
		);
	}
	arr=(AV*)(SvRV(phs->sv));

	/* If no data is returned, just clear the array. */
	if( phs->array_numstruct <= 0 ){
		av_clear(arr);
		return 1;
	}
	/* Delete extra data from array, if any */
	while( av_len(arr) >= phs->array_numstruct ){
		av_delete(arr,av_len(arr),G_DISCARD);
	};
	/* Extend array, if needed. */
	if( av_len(arr)+1 < phs->array_numstruct ){
		av_extend(arr,phs->array_numstruct-1);
	}
	/* Fill array with buffer data */
	{
	/* phs_t */
	int i; /* Not to require C99 mode */
	for(i=0;i<phs->array_numstruct;i++){
		SV *item,**pitem;
		pitem=av_fetch(arr,i,0);
		if( pitem ){
			item=*pitem;
		}
		else{
			item=NULL;
		}
		if( phs->array_indicators[i] == -1 ){
		/* NULL */
			if( item ){
				SvSetMagicSV(item,&PL_sv_undef);
				if (trace_level >= 3 || dbd_verbose >= 3 ){
					PerlIO_printf(DBIc_LOGPIO(imp_sth),
						"dbd_phs_varchar_table_posy_exe(): arr[%d] = undef; SvSetMagicSV(item,&PL_sv_undef);\n",i);
				}
			}
			else{
				av_store(arr,i,&PL_sv_undef);
				if (trace_level >= 3 || dbd_verbose >= 3 ){
					PerlIO_printf(DBIc_LOGPIO(imp_sth),
						"dbd_phs_varchar_table_posy_exe(): arr[%d] = undef; av_store(arr,i,&PL_sv_undef);\n",i);
				}
			}
		}
		else{
			if( (phs->array_indicators[i] == -2) || (phs->array_indicators[i] > 0) ){
			/* Truncation occurred */
				if (trace_level >= 2 || dbd_verbose >= 3 ){
					PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_varchar_table_posy_exe(): Placeholder '%s': data truncated at %d row.\n",
							phs->name,i);
				}
			}
			else{
				/* All OK. Just copy value.*/
			}
			if( item ){
				sv_setpvn_mg(item,phs->array_buf+phs->maxlen*i,phs->array_lengths[i]);
				SvPOK_only_UTF8(item);
				if (trace_level >= 3 || dbd_verbose >= 3 ){
					PerlIO_printf(DBIc_LOGPIO(imp_sth),
						"dbd_phs_varchar_table_posy_exe(): arr[%d] = '%s'; "
						"sv_setpvn_mg(item,phs->array_buf+phs->maxlen*i,phs->array_lengths[i]); \n",
						i, phs->array_buf+phs->maxlen*i
					);
				}
			}
			else{
				av_store(arr,i,newSVpvn(phs->array_buf+phs->maxlen*i,phs->array_lengths[i]));
				if (trace_level >= 3 || dbd_verbose >= 3 ){
					PerlIO_printf(DBIc_LOGPIO(imp_sth),
						"dbd_phs_varchar_table_posy_exe(): arr[%d] = '%s'; "
						"av_store(arr,i,newSVpvn(phs->array_buf+phs->maxlen*i,phs->array_lengths[i])); \n",
						i, phs->array_buf+phs->maxlen*i
					);
				}
			}
		}
	}
	}
	if (trace_level >= 2 || dbd_verbose >= 3 ){
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
			"dbd_phs_varchar_table_posy_exe(): scalar(@arr)=%ld.\n",
			(long)av_len(arr)+1);
	}
	return 1;
}

/* bind of SYS.DBMS_SQL.NUMBER_TABLE */
int dbd_rebind_ph_number_table(SV *sth, imp_sth_t *imp_sth, phs_t *phs) {
	dTHX;
	/*D_imp_dbh_from_sth ;*/
	sword status;
	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	AV *arr;
	int need_allocate_rows;
	int buflen;
	int numarrayentries;
	/*int flag_data_is_utf8=0;*/

	if( ( ! SvROK(phs->sv) )  || (SvTYPE(SvRV(phs->sv))!=SVt_PVAV) ) { /* Allow only array binds */
        croak("dbd_rebind_ph_number_table(): bad bind variable. ARRAY reference required, but got %s for '%s'.",
              neatsvpv(phs->sv,0), phs->name);
	}
	/* Default bind type for number table is double. */
	if( ! phs->ora_internal_type ){
        phs->ora_internal_type=SQLT_FLT;
	}else{
        if(	 (phs->ora_internal_type != SQLT_FLT) &&
             (phs->ora_internal_type != SQLT_INT) ){
            croak("dbd_rebind_ph_number_table(): Specified internal bind type %d unsupported. "
                  "SYS.DBMS_SQL.NUMBER_TABLE can be bound only to SQLT_FLT or SQLT_INT datatypes.",
                  phs->ora_internal_type);
        }
	}
	arr=(AV*)(SvRV(phs->sv));

	if (trace_level >= 2 || dbd_verbose >= 3 ){
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph_number_table(): array_numstruct=%d\n",
            phs->array_numstruct);
	}
	/* If no number of entries to bind specified,*/
	/* set phs->array_numstruct to the scalar(@array) bound.*/
	/* av_len() returns last array index, or -1 is array is empty */
	numarrayentries=av_len( arr );

	if( numarrayentries >= 0 ){
		phs->array_numstruct = numarrayentries+1;
		if (trace_level >= 2 || dbd_verbose >= 3 ){
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_number_table(): array_numstruct=%d (calculated) \n",
				phs->array_numstruct);
		}
	}

	/* Calculate each bound structure maxlen.
	 * maxlen(int) = sizeof(int);
	 * maxlen(double) = sizeof(double);
	 */
	switch( phs->ora_internal_type ){
      case SQLT_INT:
		phs->maxlen=sizeof(int);
		break;
      case SQLT_FLT:
      default:
		phs->maxlen=sizeof(double);
	}
	if (trace_level >= 2 || dbd_verbose >= 3 ){
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph_number_table(): phs->maxlen calculated  =%ld\n",
            (long)phs->maxlen);
	}

	if( phs->array_numstruct == 0 ){
		/* Oracle doesn't allow NULL buffers even for empty tables. Don't know why. */
		phs->array_numstruct=1;
	}
	if( phs->ora_maxarray_numentries== 0 ){
        /* Zero means "use current array length". */
		phs->ora_maxarray_numentries=phs->array_numstruct;

		if (trace_level >= 2 || dbd_verbose >= 3 ){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_number_table(): ora_maxarray_numentries "
                "assumed=phs->array_numstruct=%d\n",
				phs->array_numstruct);
		}
	}else{
		if (trace_level >= 2 || dbd_verbose >= 3 ){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_number_table(): ora_maxarray_numentries=%d\n",
                phs->ora_maxarray_numentries);
		}
	}

	need_allocate_rows=phs->ora_maxarray_numentries;

	if( need_allocate_rows< phs->array_numstruct ){
        need_allocate_rows=phs->array_numstruct;
	}
	buflen=need_allocate_rows* phs->maxlen; /* We need buffer for at least ora_maxarray_numentries entries */

	/* Upgrade array buffer to new length */
	if( ora_realloc_phs_array(phs,need_allocate_rows,buflen) ){
        croak("Unable to bind %s - %d structures by %d bytes requires too much memory.",
              phs->name, need_allocate_rows, buflen );
	}else{
        if (trace_level >= 2 || dbd_verbose >= 3 ){
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "dbd_rebind_ph_number_table(): ora_realloc_phs_array(,"
                "need_allocate_rows=%d,buflen=%d) succeeded.\n",
                need_allocate_rows,buflen);
        }
	}
	/* If maximum allowed bind numentries is less than allowed,
	 * do not bind full array
	 */
	if( phs->array_numstruct > phs->ora_maxarray_numentries ){
        phs->array_numstruct = phs->ora_maxarray_numentries;
	}
	/* Fill array buffer with data */

	{
        int i; /* Not to require C99 mode */
        for(i=0;i<av_len(arr)+1;i++){
            SV *item;
            item=*(av_fetch(arr,i,0));
            if( item ){
                switch( phs->ora_internal_type ){
                  case SQLT_INT:
                  {
                      int ival	 =0;
                      int val_found=0;
                      /* Double values are converted as int(val) */
                      if( SvOK( item ) && ! SvIOK( item ) ){
                          double val=SvNVx( item );
                          if( SvNOK( item ) ){
                              ival=(int) val;
                              val_found=1;
                          }
                      }
                      /* Convert item, if possible. */
                      if( (!val_found) && SvOK( item ) && ! SvIOK( item ) ){
                          SvIVx( item );
                      }
                      if( SvIOK( item ) || val_found ){
                          if( ! val_found ){
                              ival=SvIV( item );
                          }
                          /* as phs->array_buf=malloc(), proper alignment is guaranteed */
                          *(int*)(phs->array_buf+phs->maxlen*i)=ival;
                          phs->array_indicators[i]=0;
                      }else{
                          if( SvOK( item ) ){
                              /* Defined NaN assumed =0 */
                              *(int*)(phs->array_buf+phs->maxlen*i)=0;
                              phs->array_indicators[i]=0;
                          }else{
                              /* NULL */
                              phs->array_indicators[i]=1;
                          }
                      }
                      phs->array_lengths[i]=sizeof(int);
                      if (trace_level >= 3 || dbd_verbose >= 3 ){
                          PerlIO_printf(
                              DBIc_LOGPIO(imp_sth), "dbd_rebind_ph_number_table(): "
                              "(integer) array[%d]=%d%s\n",
                              i, *(int*)(phs->array_buf+phs->maxlen*i),
                              phs->array_indicators[i] ? " (NULL)" : "" );
                      }
                  }
                  break;
                  case SQLT_FLT:
                  default:
                  {
                      phs->ora_internal_type=SQLT_FLT; /* Just in case */
                      /* Convert item, if possible. */
                      if( SvOK( item ) && ! SvNOK( item ) ){
                          SvNVx( item );
                      }
                      if( SvNOK( item ) ){
                          double val=SvNVx( item );
                          /* as phs->array_buf=malloc(), proper alignment is guaranteed */
                          *(double*)(phs->array_buf+phs->maxlen*i)=val;
                          phs->array_indicators[i]=0;
                          if (trace_level >= 3 || dbd_verbose >= 3 ){
                              PerlIO_printf(
                                  DBIc_LOGPIO(imp_sth),
                                  "dbd_rebind_ph_number_table(): "
                                  "let (double) array[%d]=%f - NOT NULL\n",
                                  i, val);
                          }
                      }else{
                          if( SvOK( item ) ){
                              /* Defined NaN assumed =0 */
                              *(double*)(phs->array_buf+phs->maxlen*i)=0;
                              phs->array_indicators[i]=0;
                              if (trace_level >= 2 || dbd_verbose >= 3 ){
                                  STRLEN l;
                                  char *p=SvPV(item,l);

                                  PerlIO_printf(
                                      DBIc_LOGPIO(imp_sth),
                                      "dbd_rebind_ph_number_table(): "
                                      "let (double) array[%d]=\"%s\" =NaN. Set =0 - NOT NULL\n",
                                      i, p ? p : "<NULL>" );
                              }
                          }else{
                              /* NULL */
                              phs->array_indicators[i]=1;
                              if (trace_level >= 3 || dbd_verbose >= 3 ){
                                  PerlIO_printf(
                                      DBIc_LOGPIO(imp_sth),
                                      "dbd_rebind_ph_number_table(): "
                                      "let (double) array[%d] NULL\n",
                                      i);
                              }
                          }
                      }
                      phs->array_lengths[i]=sizeof(double);
                      if (trace_level >= 3 || dbd_verbose >= 3 ){
                          PerlIO_printf(
                              DBIc_LOGPIO(imp_sth),
                              "dbd_rebind_ph_number_table(): "
                              "(double) array[%d]=%f%s\n",
                              i, *(double*)(phs->array_buf+phs->maxlen*i),
                              phs->array_indicators[i] ? " (NULL)" : "" );
                      }
                  }
                  break;
                }
            }else{
                /* item not defined, mark NULL */
                phs->array_indicators[i]=1;
                if (trace_level >= 3 || dbd_verbose >= 3 ){
                    PerlIO_printf(
                        DBIc_LOGPIO(imp_sth),
                        "dbd_rebind_ph_number_table(): "
                        "Copying length=? array[%d]=NULL av_fetch failed.\n", i);
                }
            }
        }
	}
	/* Do actual bind */
	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
                           (text*)phs->name, (sb4)strlen(phs->name),
                           phs->array_buf,
                           phs->maxlen,
                           (ub2)phs->ora_internal_type, phs->array_indicators,
                           phs->array_lengths,
                           NULL,
                           (ub4)phs->ora_maxarray_numentries, /* max elements that can fit in allocated array	*/
                           (ub4 *)&(phs->array_numstruct),	/* (ptr to) current number of elements in array	*/
                           OCI_DEFAULT,				/* OCI_DATA_AT_EXEC (bind with callbacks) or OCI_DEFAULT  */
                           status
                           );
	if (status != OCI_SUCCESS) {
        oci_error(sth, imp_sth->errhp, status, "OCIBindByName");
        return 0;
	}
	OCIBindArrayOfStruct_log_stat(imp_sth, phs->bndhp, imp_sth->errhp,
                                  (unsigned)phs->maxlen,			/* Skip parameter for the next data value */
                                  (unsigned)sizeof(OCIInd),		/* Skip parameter for the next indicator value */
                                  (unsigned)sizeof(unsigned short), /* Skip parameter for the next actual length value */
                                  0,								/* Skip parameter for the next column-level error code */
                                  status);
	if (status != OCI_SUCCESS) {
        oci_error(sth, imp_sth->errhp, status, "OCIBindArrayOfStruct");
        return 0;
	}
	if (phs->maxdata_size) {
        OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4)OCI_HTYPE_BIND,
                            phs->array_buf, (ub4)phs->array_buflen, (ub4)OCI_ATTR_MAXDATA_SIZE, imp_sth->errhp, status);
        if ( status != OCI_SUCCESS ) {
            oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_MAXDATA_SIZE)"));
            return 0;
        }
	}

	return 2;
}


/* Copy array data from array buffer into perl array */
/* Returns false on error, true on success */
int dbd_phs_number_table_post_exe(imp_sth_t *imp_sth, phs_t *phs){
	dTHX;

	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	AV *arr;

	if( ( ! SvROK(phs->sv) )  || (SvTYPE(SvRV(phs->sv))!=SVt_PVAV) ) { /* Allow only array binds */
	croak("dbd_phs_number_table_post_exe(): bad bind variable. ARRAY reference required, but got %s for '%s'.",
			neatsvpv(phs->sv,0), phs->name);
	}
	if (trace_level >= 1 || dbd_verbose >= 3 ){
        PerlIO_printf(DBIc_LOGPIO(imp_sth),
		"dbd_phs_number_table_post_exe(): Called for '%s' : array_numstruct=%d, maxlen=%ld \n",
		phs->name,
		phs->array_numstruct,
		(long)phs->maxlen
		);
	}
	/* At this point, ora_internal_type can't be default. It must be set at bind time. */
	if(	 (phs->ora_internal_type != SQLT_FLT) &&
		(phs->ora_internal_type != SQLT_INT) ){
	croak("dbd_rebind_ph_number_table(): Specified internal bind type %d unsupported. "
		"SYS.DBMS_SQL.NUMBER_TABLE can be bound only to SQLT_FLT, SQLT_INT datatypes.",
		phs->ora_internal_type);
	}
	arr=(AV*)(SvRV(phs->sv));

	/* If no data is returned, just clear the array. */
	if( phs->array_numstruct <= 0 ){
	av_clear(arr);
	return 1;
	}
	/* Delete extra data from array, if any */
	while( av_len(arr) >= phs->array_numstruct ){
	av_delete(arr,av_len(arr),G_DISCARD);
	};
	/* Extend array, if needed. */
	if( av_len(arr)+1 < phs->array_numstruct ){
	av_extend(arr,phs->array_numstruct-1);
	}
	/* Fill array with buffer data */
	{
	/* phs_t */
	int i; /* Not to require C99 mode */
	for(i=0;i<phs->array_numstruct;i++){
		SV *item,**pitem;
		pitem=av_fetch(arr,i,0);
		if( pitem ){
		item=*pitem;
		}else{
		item=NULL;
		}
		if( phs->array_indicators[i] == -1 ){
		/* NULL */
		if( item ){
			SvSetMagicSV(item,&PL_sv_undef);
			if (trace_level >= 3 || dbd_verbose >= 3 ){
                PerlIO_printf(DBIc_LOGPIO(imp_sth),
				"dbd_phs_number_table_post_exe(): arr[%d] = undef; SvSetMagicSV(item,&PL_sv_undef);\n",
				i
				);
			}
		}else{
			av_store(arr,i,&PL_sv_undef);
			if (trace_level >= 3 || dbd_verbose >= 3 ){
                PerlIO_printf(DBIc_LOGPIO(imp_sth),
				"dbd_phs_number_table_post_exe(): arr[%d] = undef; av_store(arr,i,&PL_sv_undef);\n",
				i
				);
			}
		}
		}else{
		if( (phs->array_indicators[i] == -2) || (phs->array_indicators[i] > 0) ){
			/* Truncation occurred */
			if (trace_level >= 2 || dbd_verbose >= 3 ){
                PerlIO_printf(DBIc_LOGPIO(imp_sth),
				"dbd_phs_number_table_post_exe(): Placeholder '%s': data truncated at %d row.\n",
				phs->name,i);
			}
		}else{
			/* All OK. Just copy value.*/
		}
		if( item ){
			switch(phs->ora_internal_type){
			case SQLT_INT:
				if (trace_level >= 4 || dbd_verbose >= 4 ){
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_number_table_post_exe(): (int) set arr[%d] = %d \n",
					i, *(int*)(phs->array_buf+phs->maxlen*i)
					);
				}
				sv_setiv_mg(item,*(int*)(phs->array_buf+phs->maxlen*i));
				break;
			case SQLT_FLT:
				if (trace_level >= 4 || dbd_verbose >= 4 ){
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_number_table_post_exe(): (double) set arr[%d] = %f \n",
					i, *(double*)(phs->array_buf+phs->maxlen*i)
					);
				}
				sv_setnv_mg(item,*(double*)(phs->array_buf+phs->maxlen*i));
			}
			if (trace_level >= 3 || dbd_verbose >= 3 ){
			STRLEN l;
			char *str= SvPOK(item) ? SvPV(item,l) : "<unprintable>" ;
			PerlIO_printf(DBIc_LOGPIO(imp_sth),
				"dbd_phs_number_table_post_exe(): arr[%d] = '%s'\n",
					i, str ? str : "<unprintable>"
				);
			}
		}else{
			switch(phs->ora_internal_type){
			case SQLT_INT:
				if (trace_level >= 4 || dbd_verbose >= 4 ){
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_number_table_post_exe(): (int) store new arr[%d] = %d \n",
					i, *(int*)(phs->array_buf+phs->maxlen*i)
				);
				}
				av_store(arr,i,newSViv( *(int*)(phs->array_buf+phs->maxlen*i) ));
				break;
			case SQLT_FLT:
				if (trace_level >= 4 || dbd_verbose >= 4 ){
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_number_table_post_exe(): (double) store new arr[%d] = %f \n",
					i, *(double*)(phs->array_buf+phs->maxlen*i)
					);
				}
				av_store(arr,i,newSVnv( *(double*)(phs->array_buf+phs->maxlen*i) ));
			}
			if (trace_level >= 3 || dbd_verbose >= 3 ){
				STRLEN l;
				char *str;
				SV**pitem=av_fetch(arr,i,0);
				if( pitem ){
					item=*pitem;
				}
				str= item ? ( SvPOK(item) ? SvPV(item,l) : "<unprintable>"  ) : "<undef>";
				PerlIO_printf(DBIc_LOGPIO(imp_sth),
					"dbd_phs_number_table_post_exe(): arr[%d] = '%s'\n",
					i, str ? str : "<unprintable>"
				);
			}
		}
		}
	}
	}
	if (trace_level >= 2 || dbd_verbose >= 3 ){
        PerlIO_printf(DBIc_LOGPIO(imp_sth),
		"dbd_phs_number_table_post_exe(): scalar(@arr)=%ld.\n",
		(long)av_len(arr)+1);
	}
	return 1;
}




static int
dbd_rebind_ph_char(imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	STRLEN value_len;
	int at_exec = 0;
	at_exec = (phs->desc_h == NULL);

	if (!SvPOK(phs->sv)) {	/* normalizations for special cases	*/
		if (SvOK(phs->sv)) {	/* ie a number, convert to string ASAP	*/
			if (!(SvROK(phs->sv) && phs->is_inout))
				sv_2pv(phs->sv, &PL_na);
		}
		else /* ensure we're at least an SVt_PV (so SvPVX etc work)	*/
			(void) SvUPGRADE(phs->sv, SVt_PV);
	}


	if (DBIc_DBISTATE(imp_sth)->debug >= 2 || dbd_verbose >= 3 ) {
		char *val = neatsvpv(phs->sv,10);
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph_char() (1): bind %s <== %.1000s (", phs->name, val);
		if (!SvOK(phs->sv))
			PerlIO_printf(DBIc_LOGPIO(imp_sth), "NULL, ");
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "size %ld/%ld/%ld, ",
            (long)SvCUR(phs->sv),(long)SvLEN(phs->sv),(long)phs->maxlen);
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "ptype %d(%s), otype %d %s)\n",
            (int)SvTYPE(phs->sv), sql_typecode_name(phs->ftype),
            phs->ftype,(phs->is_inout) ? ", inout" : "");
	}

	/* At the moment we always do sv_setsv() and rebind.	*/
	/* Later we may optimise this so that more often we can	*/
	/* just copy the value & length over and not rebind.	*/

	if (phs->is_inout) {	/* XXX */
		if (SvREADONLY(phs->sv))
			croak("Modification of a read-only value attempted");
		if (imp_sth->ora_pad_empty)
			croak("Can't use ora_pad_empty with bind_param_inout");
		if (SvTYPE(phs->sv)!=SVt_RV || !at_exec) {
			if (phs->ftype == 96){
                SvGROW(phs->sv,(STRLEN) (unsigned int)phs->maxlen-1);
                if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6) {
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
                                  "Growing 96 phs sv to %ld resulted in buffer %ld\n", phs->maxlen - 1, SvLEN(phs->sv));
                }
			} else {
				STRLEN min_len = 28;
				(void)SvUPGRADE(phs->sv, SVt_PVNV);
                /* ensure room for result, 28 is magic number (see sv_2pv)	*/
                /* don't apply 28 char min to CHAR types - probably shouldn't	*/
                /* apply it anywhere really, trying to be too helpful.		*/
                /* phs->sv _is_ the real live variable, it may 'mutate' later	*/
                /* pre-upgrade to high'ish type to reduce risk of SvPVX realloc/move */
                /* NOTE SvGROW resets SvOOK_offset and we want to do this */
                SvGROW(phs->sv, (STRLEN)(((unsigned int) phs->maxlen <= min_len) ? min_len : (unsigned int) phs->maxlen));
                if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6) {
                    PerlIO_printf(DBIc_LOGPIO(imp_sth),
                                  "Growing phs sv to %ld resulted in buffer %ld\n", phs->maxlen +1, SvLEN(phs->sv));
                }
			}
		}

	}

	/* At this point phs->sv must be at least a PV with a valid buffer,	*/
	/* even if it's undef (null)					*/
	/* Here we set phs->progv, phs->indp, and value_len.		*/

	if (SvOK(phs->sv)) {
		phs->progv = SvPV(phs->sv, value_len);
		phs->indp  = 0;
	} else {	/* it's null but point to buffer incase it's an out var	*/
		phs->progv = (phs->is_inout) ? SvPVX(phs->sv) : NULL;
		phs->indp  = -1;
		value_len  = 0;
	}


	if (imp_sth->ora_pad_empty && value_len==0) {
 		sv_setpv(phs->sv, " ");
		phs->progv = SvPV(phs->sv, value_len);
	}

	phs->sv_type = SvTYPE(phs->sv);	/* part of mutation check	*/
	if (SvTYPE(phs->sv) == SVt_RV && SvTYPE(SvRV(phs->sv)) == SVt_PVAV) { /* it is returning an array of scalars not a single scalar*/
		phs->maxlen  = 4000; /* Just make is a varchar max should be ok for most things*/

	} else {
        if (DBIc_DBISTATE(imp_sth)->debug >= 6|| dbd_verbose >= 6 ) {
            PerlIO_printf(DBIc_LOGPIO(imp_sth),
                          "Changing maxlen to %ld\n", SvLEN(phs->sv));
        }
		phs->maxlen  = ((IV)SvLEN(phs->sv)); /* avail buffer space (64bit safe) Logicaly maxlen should never change but it does why I know not - MJE because SvGROW can allocate more than you ask for - anyway - I fixed that and it doesn't grow anymore */

	}


	if (phs->maxlen < 0)		/* can happen with nulls	*/
		phs->maxlen = 0;

	phs->alen = value_len + phs->alen_incnull;

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 ) {
		/*UV neatsvpvlen = (UV)DBIc_DBISTATE(imp_sth)->neatsvpvlen;*/
		char *val = neatsvpv(phs->sv,10);
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph_char() (2): bind %s <== %.1000s (size %ld/%ld, "
            "otype %d(%s), indp %d, at_exec %d)\n",
			phs->name,
			(phs->progv) ?  val: "",
			(long)phs->alen, (long)phs->maxlen,
            phs->ftype,sql_typecode_name(phs->ftype), phs->indp, at_exec);
	}

	return 1;
}


/*
* Rebind an "in" cursor ref to its real statement handle
* This allows passing cursor refs as "in" to pl/sql (but only if you got the
* cursor from pl/sql to begin with)
*/
int
pp_rebind_ph_rset_in(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	dTHR;
	SV * sth_csr = phs->sv;
	D_impdata(imp_sth_csr, imp_sth_t, sth_csr);
	sword status;

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	pp_rebind_ph_rset_in: BEGIN\n	calling OCIBindByName(stmhp=%p, "
            "bndhp=%p, errhp=%p, name=%s, csrstmhp=%p, ftype=%d)\n",
            imp_sth->stmhp, phs->bndhp, imp_sth->errhp, phs->name,
            imp_sth_csr->stmhp, phs->ftype);

	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
			(text*)phs->name, (sb4)strlen(phs->name),
			&imp_sth_csr->stmhp,
			0,
			(ub2)phs->ftype, 0,
			NULL,
			0, 0,
			NULL,
			(ub4)OCI_DEFAULT,
			status
			);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindByName SQLT_RSET");
		return 0;
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth), "	pp_rebind_ph_rset_in: END\n");

	return 2;
}


int
pp_exec_rset(SV *sth, imp_sth_t *imp_sth, phs_t *phs, int pre_exec)
{
    dTHX;

	if (pre_exec) {	/* pre-execute - throw away previous descriptor and rebind */
		sword status;

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                " pp_exec_rset bind %s - allocating new sth...\n",
                phs->name);

        /* extproc deallocates everything for us */
		if (is_extproc)
			return 1;

		if (!phs->desc_h || 1) { /* XXX phs->desc_t != OCI_HTYPE_STMT) */
			if (phs->desc_h) {
				OCIHandleFree_log_stat(imp_sth, phs->desc_h, phs->desc_t, status);
				phs->desc_h = NULL;
			}
			phs->desc_t = OCI_HTYPE_STMT;
			OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &phs->desc_h, phs->desc_t, status);
		 }

		phs->progv = (char*)&phs->desc_h;
		phs->maxlen = 0;

		OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
			(text*)phs->name,
			(sb4)strlen(phs->name),
			phs->progv,
			0,
			(ub2)phs->ftype,
            /* I, MJE have no evidence that passing an indicator to this func
               causes ORA-01001 (invalid cursor) errors. Also, without it
               you cannot test the indicator to check we have a valid output
               parameter. However, it would seem when you do specify an
               indicator it always comes back as 0 so it is useless. */
			NULL, /* using &phs->indp triggers ORA-01001 errors! */
			NULL,
			0,
			0,
			NULL,
			OCI_DEFAULT,
			status);

		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCIBindByName SQLT_RSET");
			return 0;
		}

        /*
          NOTE: The code used to magic a DBI stmt handle into existence
          here before even knowing if the output parameter was going to
          be a valid open cursor. The code to do this moved to post execute
          below. See RT 82663 - Errors if a returned SYS_REFCURSOR is not opened
        */
	}
	else {		/* post-execute - setup the statement handle */
		dTHR;
		dSP;
		D_imp_dbh_from_sth;
		HV *init_attr = newHV();
		int count;
        ub4 stmt_state = 99;
        sword status;
		SV * sth_csr;

        /* Before we go to the bother of attempting to allocate a new sth
           for this cursor make sure the Oracle sth is executed i.e.,
           the returned cursor may never have been opened */
        OCIAttrGet_stmhp_stat2(imp_sth, (OCIStmt*)phs->desc_h, &stmt_state, 0,
                               OCI_ATTR_STMT_STATE, status);
        if (status != OCI_SUCCESS) {
            oci_error(sth, imp_sth->errhp, status, "OCIAttrGet OCI_ATTR_STMT_STATE");
            return 0;
        }
        if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 ) {
            /* initialized=1, executed=2, end of fetch=3 */
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	returned cursor/statement state: %u\n", stmt_state);
        }

        /* We seem to get an indp of 0 even for a cursor which was never
           opened and set to NULL. If this is the case we check the stmt state
           and find the cursor is initialized but not executed - there is no
           point in going any further if it is not executed - just return undef.
           See RT 82663 */
        if (stmt_state == OCI_STMT_STATE_INITIALIZED) {
			OCIHandleFree_log_stat(imp_sth, (OCIStmt *)phs->desc_h,
                                   OCI_HTYPE_STMT, status);
			if (status != OCI_SUCCESS) {
				oci_error(sth, imp_sth->errhp, status, "OCIHandleFree");
                return 0;
            }
            phs->desc_h = NULL;
            phs->sv = newSV(0);                 /* undef */
            return 1;
        }

        /* Now we know we have an executed cursor create a new sth */
		ENTER;
		SAVETMPS;
		PUSHMARK(SP);
		XPUSHs(sv_2mortal(newRV((SV*)DBIc_MY_H(imp_dbh))));
		XPUSHs(sv_2mortal(newRV((SV*)init_attr)));
		PUTBACK;
		count = perl_call_pv("DBI::_new_sth", G_ARRAY);
		SPAGAIN;

		if (count != 2)
			 croak("panic: DBI::_new_sth returned %d values instead of 2", count);

		(void)POPs;			/* discard inner handle */
		sv_setsv(phs->sv, POPs); 	/* save outer handle */
		SvREFCNT_dec(init_attr);
		PUTBACK;
		FREETMPS;
		LEAVE;
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "   pp_exec_rset   bind %s - allocated %s...\n",
                phs->name, neatsvpv(phs->sv, 0));

        sth_csr = phs->sv;

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	   bind %s - initialising new %s for cursor 0x%lx...\n",
                phs->name, neatsvpv(sth_csr,0), (unsigned long)phs->progv);

        {
            D_impdata(imp_sth_csr, imp_sth_t, sth_csr); /* TO_DO */

            /* copy appropriate handles and attributes from parent statement	*/
            imp_sth_csr->envhp		= imp_sth->envhp;
            imp_sth_csr->errhp		= imp_sth->errhp;
            imp_sth_csr->srvhp		= imp_sth->srvhp;
            imp_sth_csr->svchp		= imp_sth->svchp;
            imp_sth_csr->auto_lob	= imp_sth->auto_lob;
            imp_sth_csr->pers_lob	= imp_sth->pers_lob;
            imp_sth_csr->clbk_lob	= imp_sth->clbk_lob;
            imp_sth_csr->piece_size	= imp_sth->piece_size;
            imp_sth_csr->piece_lob	= imp_sth->piece_lob;
            imp_sth_csr->is_child	= 1; /*no prefetching on a cursor or sp*/


            /* assign statement handle from placeholder descriptor	*/
            imp_sth_csr->stmhp = (OCIStmt*)phs->desc_h;
            phs->desc_h = NULL;		  /* tell phs that we own it now	*/

            /* force stmt_type since OCIAttrGet(OCI_ATTR_STMT_TYPE) doesn't work! */
            imp_sth_csr->stmt_type = OCI_STMT_SELECT;
            DBIc_IMPSET_on(imp_sth_csr);

            /* set ACTIVE so dbd_describe doesn't do explicit OCI describe */
            DBIc_ACTIVE_on(imp_sth_csr);
            if (!dbd_describe(sth_csr, imp_sth_csr)) {
                return 0;
            }
        }
	}

	return 1;

}

static int
dbd_rebind_ph_xml( SV* sth, imp_sth_t *imp_sth, phs_t *phs) {
dTHX;
dTHR;
OCIType *tdo = NULL;
sword status;
 SV* ptr;


	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth), " in  dbd_rebind_ph_xml\n");

/*go and create the XML dom from the passed in value*/

	phs->sv=createxmlfromstring(sth, imp_sth, phs->sv );

	if (phs->is_inout)
		croak("OUT binding for NTY is currently unsupported");

	/* ensure that the value is a support named object type */
	/* (currently only OCIXMLType*)						 */
	if ( sv_isa(phs->sv, "OCIXMLTypePtr") ) {
        /* TO_DO not logging: */
		OCITypeByName_log(
            imp_sth,
            imp_sth->envhp,
            imp_sth->errhp,
            imp_sth->svchp,
            (CONST text*)"SYS", 3,    /* schema_name, schema_length */
            (CONST text*)"XMLTYPE", 7, /* type_name, type_length */
            (CONST text*)0, 0,         /* version_name, version_length */
            OCI_DURATION_CALLOUT,      /* pin_duration */
            OCI_TYPEGET_HEADER,        /* get_option */
            &tdo,                      /* tdo */
            status);
		ptr = SvRV(phs->sv);
		phs->progv  = (void*) SvIV(ptr);
		phs->maxlen = sizeof(OCIXMLType*);
	}
	else
		croak("Unsupported named object type for bind parameter");


	/* bind by name */

	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
 			(text*)phs->name, (sb4)strlen(phs->name),
 			(dvoid *) NULL, /* value supplied in BindObject later */
 			0,
 			(ub2)phs->ftype, 0,
 			NULL,
 			0, 0,
 			NULL,
 			(ub4)OCI_DEFAULT,
 			status
 			);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindByName SQLT_NTY");
		return 0;
	}
	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
 		PerlIO_printf(DBIc_LOGPIO(imp_sth), "	pp_rebind_ph_nty: END\n");


	 /* bind the object */
	 OCIBindObject(phs->bndhp, imp_sth->errhp,
 		(CONST OCIType*)tdo,
 		(dvoid **)&phs->progv,
 		(ub4*)NULL,
 		(dvoid **)NULL,
 		(ub4*)NULL);

	return 2;
}


static int
dbd_rebind_ph(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	/*ub2 *alen_ptr = NULL;*/
	sword status;
	int done = 0;
	int at_exec;
	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	ub1 csform;
	ub2 csid;

	if (trace_level >= 5 || dbd_verbose >= 5 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph() (1): rebinding %s as %s (%s, ftype %d (%s), "
            "csid %d, csform %d(%s), inout %d)\n",
            phs->name, (SvPOK(phs->sv) ? neatsvpv(phs->sv,10) : "NULL"),
            (SvUTF8(phs->sv) ? "is-utf8" : "not-utf8"),
            phs->ftype,sql_typecode_name(phs->ftype), phs->csid, phs->csform,
            oci_csform_name(phs->csform), phs->is_inout);

	switch (phs->ftype) {
		case ORA_VARCHAR2_TABLE:
			done = dbd_rebind_ph_varchar2_table(sth, imp_sth, phs);
			break;
		case ORA_NUMBER_TABLE:
			done = dbd_rebind_ph_number_table(sth, imp_sth, phs);
			break;
		case SQLT_CLOB:
		case SQLT_BLOB:
			done = dbd_rebind_ph_lob(sth, imp_sth, phs);
			break;
		case SQLT_RSET:
			done = dbd_rebind_ph_rset(sth, imp_sth, phs);
			break;
		 case ORA_XMLTYPE:
			done = dbd_rebind_ph_xml(sth, imp_sth, phs);
	 		break;
		default:
			done = dbd_rebind_ph_char(imp_sth, phs);
	}

	if (done == 2) { /* the dbd_rebind_* did the OCI bind call itself successfully */
		if (trace_level >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth), "	  rebind %s done with ftype %d (%s)\n",
				phs->name, phs->ftype,sql_typecode_name(phs->ftype));
		return 1;
	}

	if (trace_level >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth), "	  bind %s as ftype %d (%s)\n",
		phs->name, phs->ftype,sql_typecode_name(phs->ftype));

	if (done != 1) {
		return 0;	 /* the rebind failed	*/
	}

	at_exec = (phs->desc_h == NULL);


	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
		(text*)phs->name, (sb4)strlen(phs->name),
		phs->progv,
		phs->maxlen ? (sb4)phs->maxlen : 1,	/* else bind "" fails	*/
		(ub2)phs->ftype, &phs->indp,
		NULL,	/* ub2 *alen_ptr not needed with OCIBindDynamic */
		&phs->arcode,
		0,		/* max elements that can fit in allocated array	*/
		NULL,	/* (ptr to) current number of elements in array	*/
		(ub4)(at_exec ? OCI_DATA_AT_EXEC : OCI_DEFAULT),
		status
	);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindByName");
		return 0;
	}
	if (at_exec) {
		OCIBindDynamic_log(imp_sth, phs->bndhp, imp_sth->errhp,
			(dvoid *)phs, dbd_phs_in,
			(dvoid *)phs, dbd_phs_out, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindDynamic");
		return 0;
	}
	}

	/* some/all of the following should perhaps move into dbd_phs_in() */

	csform = phs->csform;

	if (!csform && SvUTF8(phs->sv)) {
		/* try to default csform to avoid translation through non-unicode */
		if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT))		/* prefer IMPLICIT */
 			csform = SQLCS_IMPLICIT;
		else if (CSFORM_IMPLIES_UTF8(SQLCS_NCHAR))
			csform = SQLCS_NCHAR;	/* else leave csform == 0 */
	if (trace_level || dbd_verbose >= 3)
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph() (2): rebinding %s with UTF8 value %s", phs->name,
		(csform == SQLCS_IMPLICIT) ? "so setting csform=SQLCS_IMPLICIT" :
		(csform == SQLCS_NCHAR)	? "so setting csform=SQLCS_NCHAR" :
		"but neither CHAR nor NCHAR are unicode\n");
	}

	if (csform) {
		/* set OCI_ATTR_CHARSET_FORM before we get the default OCI_ATTR_CHARSET_ID */
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
		&csform, (ub4) 0, (ub4) OCI_ATTR_CHARSET_FORM, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_FORM)"));
			return 0;
		}
	}

	if (!phs->csid_orig) {	/* get the default csid Oracle would use */
		OCIAttrGet_log_stat(imp_sth, phs->bndhp, OCI_HTYPE_BIND, &phs->csid_orig, NULL,
		OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
	}

	/* if app has specified a csid then use that, else use default */
	csid = (phs->csid) ? phs->csid : phs->csid_orig;

	/* if data is utf8 but charset isn't then switch to utf8 csid */
	if (SvUTF8(phs->sv) && !CS_IS_UTF8(csid))
		csid = utf8_csid; /* not al32utf8_csid here on purpose */

	if (trace_level >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "dbd_rebind_ph(): bind %s <== %s "
            "(%s, %s, csid %d->%d->%d, ftype %d (%s), csform %d(%s)->%d(%s), "
            "maxlen %lu, maxdata_size %lu)\n",
            phs->name, neatsvpv(phs->sv,10),
            (phs->is_inout) ? "inout" : "in",
            (SvUTF8(phs->sv) ? "is-utf8" : "not-utf8"),
            phs->csid_orig, phs->csid, csid,
            phs->ftype, sql_typecode_name(phs->ftype), phs->csform,
            oci_csform_name(phs->csform), csform, oci_csform_name(csform),
            (unsigned long)phs->maxlen, (unsigned long)phs->maxdata_size);

	if (csid) {
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
			&csid, (ub4) 0, (ub4) OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_ID)"));
			return 0;
		}
	}

	if (phs->maxdata_size) {
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4)OCI_HTYPE_BIND,
			neatsvpv(phs->sv,0), (ub4)phs->maxdata_size, (ub4)OCI_ATTR_MAXDATA_SIZE, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_MAXDATA_SIZE)"));
			return 0;
		}
	}

	return 1;
}


int
dbd_bind_ph(SV *sth, imp_sth_t *imp_sth, SV *ph_namesv, SV *newvalue, IV sql_type, SV *attribs, int is_inout, IV maxlen)
{
	dTHX;
	SV **phs_svp;
	STRLEN name_len;
	char *name = Nullch;
	char namebuf[32];
	phs_t *phs;

	/* check if placeholder was passed as a number	*/
	if (SvGMAGICAL(ph_namesv))	/* eg tainted or overloaded */
		mg_get(ph_namesv);

	if (!SvNIOKp(ph_namesv)) {
		STRLEN i;
		name = SvPV(ph_namesv, name_len);
		if (name_len > sizeof(namebuf)-1)
			croak("Placeholder name %s too long", neatsvpv(ph_namesv,0));

		for (i=0; i<name_len; i++) namebuf[i] = toLOWER(name[i]);
			namebuf[i] = '\0';
		name = namebuf;
	}

	if (SvNIOKp(ph_namesv) || (name && isDIGIT(name[0]))) {
		sprintf(namebuf, ":p%d", (int)SvIV(ph_namesv));
		name = namebuf;
		name_len = strlen(name);
	}

	assert(name != Nullch);

	if (SvROK(newvalue)
			&& !IS_DBI_HANDLE(newvalue)	/* dbi handle allowed for cursor variables */
			&& !SvAMAGIC(newvalue)		/* overload magic allowed (untested) */
			&& !sv_derived_from(newvalue, "OCILobLocatorPtr" )  /* input LOB locator*/
			&& !(SvTYPE(SvRV(newvalue))==SVt_PVAV) /* Allow array binds */
	)
		croak("Can't bind a reference (%s)", neatsvpv(newvalue,0));

	if (SvTYPE(newvalue) > SVt_PVAV) /* Array binding supported */
		croak("Can't bind a non-scalar, non-array value (%s)", neatsvpv(newvalue,0));
	if (SvTYPE(newvalue) == SVt_PVLV && is_inout)	/* may allow later */
		croak("Can't bind ``lvalue'' mode scalar as inout parameter (currently)");

	if (DBIc_DBISTATE(imp_sth)->debug >= 2 || dbd_verbose >= 3 ) {
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth), "dbd_bind_ph(1): bind %s <== %s (type %ld (%s)",
		name, neatsvpv(newvalue,0), (long)sql_type,sql_typecode_name(sql_type));
		if (is_inout)
			PerlIO_printf(DBIc_LOGPIO(imp_sth), ", inout 0x%lx, maxlen %ld",
			(long)newvalue, (long)maxlen);
		if (attribs)
			PerlIO_printf(DBIc_LOGPIO(imp_sth), ", attribs: %s", neatsvpv(attribs,0));
		PerlIO_printf(DBIc_LOGPIO(imp_sth), ")\n");
	}

	phs_svp = hv_fetch(imp_sth->all_params_hv, name, name_len, 0);


	if (phs_svp == NULL)
		croak("Can't bind unknown placeholder '%s' (%s)", name, neatsvpv(ph_namesv,0));

		/* This value is not a string, but a binary structure phs_st instead. */
	phs = (phs_t*)(void*)SvPVX(*phs_svp);	/* placeholder struct	*/

	if (phs->sv == &PL_sv_undef) {	/* first bind for this placeholder	*/
		phs->is_inout = is_inout;
		if (is_inout) {
			/* phs->sv assigned in the code below */
			++imp_sth->has_inout_params;
			/* build array of phs's so we can deal with out vars fast	*/
			if (!imp_sth->out_params_av)
				imp_sth->out_params_av = newAV();
			av_push(imp_sth->out_params_av, SvREFCNT_inc(*phs_svp));
		}

	/*
	 * Init number of bound array entries to zero.
	 * If "ora_maxarray_numentries" bind parameter specified,
	 * it would be set below.
	 *
	 * If no ora_maxarray_numentries specified, let it be
	 * the same as scalar(@array) bound (see dbd_rebind_ph_varchar2_table() ).
	 */
		phs->array_numstruct=0;

		if (attribs) {	/* only look for ora_type on first bind of var	*/
			SV **svp;
			/* Setup / Clear attributes as defined by attribs.		*/
			/* XXX If attribs is EMPTY then reset attribs to default?	*/

			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_type",8, 0)) != NULL) {
				int ora_type = SvIV(*svp);
				if (!oratype_bind_ok(ora_type))
					croak("Can't bind %s, ora_type %d not supported by DBD::Oracle", phs->name, ora_type);
				if (sql_type)
					croak("Can't specify both TYPE (%"IVdf") and ora_type (%d) for %s", sql_type, ora_type, phs->name);
				phs->ftype = ora_type;
			}
			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_field",9, 0)) != NULL) {
				phs->ora_field = SvREFCNT_inc(*svp);
			}
			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_csform", 10, 0)) != NULL) {
				if (SvIV(*svp) == SQLCS_IMPLICIT || SvIV(*svp) == SQLCS_NCHAR)
					phs->csform = (ub1)SvIV(*svp);
				else warn("ora_csform must be 1 (SQLCS_IMPLICIT) or 2 (SQLCS_NCHAR), not %"IVdf"", SvIV(*svp));
			}
			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_maxdata_size", 16, 0)) != NULL) {
				phs->maxdata_size = SvUV(*svp);
			}
			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_maxarray_numentries", 23, 0)) != NULL) {
				phs->ora_maxarray_numentries=SvUV(*svp);
			}
			if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_internal_type", 17, 0)) != NULL) {
				phs->ora_internal_type=SvUV(*svp);
			}
		}


		if (sql_type)
			phs->ftype = ora_sql_type(imp_sth, phs->name, (int)sql_type);
	/* treat Oracle7 SQLT_CUR as SQLT_RSET for Oracle8	*/
		if (phs->ftype==102)
			phs->ftype = ORA_RSET;

	/* some types require the trailing null included in the length.	*/
	/* SQLT_STR=5=STRING, SQLT_AVC=97=VARCHAR	*/
		phs->alen_incnull = (phs->ftype==SQLT_STR || phs->ftype==SQLT_AVC);

	}	/* was first bind for this placeholder  */

	/* check later rebinds for any changes */
	else if (is_inout != phs->is_inout) {
		croak("Can't rebind or change param %s in/out mode after first bind (%d => %d)",
			phs->name, phs->is_inout , is_inout);

	}
	else if (sql_type && phs->ftype != ora_sql_type(imp_sth, phs->name, (int)sql_type)) {
		croak("Can't change TYPE of param %s to %"IVdf" after initial bind",
			phs->name, sql_type);

	}
	/* Array binding is supported for a limited number of data types. */

	if( SvROK(newvalue) ){
		if( SvTYPE(SvRV(newvalue))==SVt_PVAV ){
			if(  (phs->ftype == ORA_VARCHAR2_TABLE)	||
				 (phs->ftype == ORA_NUMBER_TABLE)	||
				 (phs->ftype == 1)) /*ORA_VARCHAR2*/ {
				/* Supported */

				/* Reload array-size-related attributes */
				if (attribs) {
					SV **svp;

					if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_maxdata_size", 16, 0)) != NULL) {
						phs->maxdata_size = SvUV(*svp);
					}
					if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_maxarray_numentries", 23, 0)) != NULL) {
						phs->ora_maxarray_numentries=SvUV(*svp);
					}
					if ( (svp=hv_fetch((HV*)SvRV(attribs), "ora_internal_type", 17, 0)) != NULL) {
						phs->ora_internal_type=SvUV(*svp);
					}
				}
			}
			else{
				/* All the other types are not supported */
				croak("Array bind is supported only for ORA_%%_TABLE types. Unable to bind '%s'.",phs->name);
			}
		}
	}

	/* Add checks for other reference types here ? */

	phs->maxlen = maxlen;		/* 0 if not inout		*/

	if (!is_inout) {	/* normal bind so take a (new) copy of current value	*/
		if (phs->sv == &PL_sv_undef)	/* (first time bind) */
			phs->sv = newSV(0);
		sv_setsv(phs->sv, newvalue);
		if (SvAMAGIC(phs->sv)) /* overloaded. XXX hack, logic ought to be pushed deeper */
			sv_pvn_force(phs->sv, &PL_na);
	} else {
        if (newvalue != phs->sv) {
            if (phs->sv)
                SvREFCNT_dec(phs->sv);

            phs->sv = SvREFCNT_inc(newvalue);	/* point to live var	*/
        }
	}

	return dbd_rebind_ph(sth, imp_sth, phs);
}


/* --- functions to 'complete' the fetch of a value --- */

void
dbd_phs_sv_complete(imp_sth_t *imp_sth, phs_t *phs, SV *sv, I32 debug)
{
	dTHX;
	char *note = "";
	/* XXX doesn't check arcode for error, caller is expected to */

	if (phs->indp == 0) {					/* is okay	  */

		if (phs->is_inout && phs->alen == SvLEN(sv)) {

			/* if the placeholder has not been assigned to then phs->alen */
			/* is left untouched: still set to SvLEN(sv). If we use that  */
			/* then we'll get garbage bytes beyond the original contents. */
			phs->alen = SvCUR(sv);
			note = " UNTOUCHED?";
		}

		if (SvPVX(sv)) {
			SvCUR_set(sv, phs->alen);
			*SvEND(sv) = '\0';
			SvPOK_only_UTF8(sv);
            if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT)) {
#ifdef sv_utf8_decode
                sv_utf8_decode(sv);
#else
                SvUTF8_on(sv);
#endif
            }
		}
		else {	/* shouldn't happen */
			debug = 2;
			dbd_verbose =3;
			note = " [placeholder has no data buffer]";
		}

		if (debug >= 2 || dbd_verbose >= 3 )
			PerlIO_printf(DBILOGFP, "  out %s = %s (arcode %d, ind %d, len %d)%s\n",
			phs->name, neatsvpv(sv,0), phs->arcode, phs->indp, phs->alen, note);
	}
	else {
		if (phs->indp > 0 || phs->indp == -2) {	 /* truncated	*/
			if (SvPVX(sv)) {
				SvCUR_set(sv, phs->alen);
				*SvEND(sv) = '\0';
				SvPOK_only_UTF8(sv);
                if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT)) {
#ifdef sv_utf8_decode
                    sv_utf8_decode(sv);
#else
                    SvUTF8_on(sv);
#endif
                }
			}
			else {	/* shouldn't happen */
				debug = 2;
				dbd_verbose =3;
				note = " [placeholder has no data buffer]";
			}
			if (debug >= 2 || dbd_verbose >= 3 )
				PerlIO_printf(DBILOGFP,
				"   out %s = %s\t(TRUNCATED from %d to %ld, arcode %d)%s\n",
					phs->name, neatsvpv(sv,0), phs->indp, (long)phs->alen, phs->arcode, note);
		}
		else {
			if (phs->indp == -1) {					  /* is NULL	  */
				(void)SvOK_off(phs->sv);
				if (debug >= 2 || dbd_verbose >= 3 )
					PerlIO_printf(DBILOGFP,
							"	   out %s = undef (NULL, arcode %d)\n",
						phs->name, phs->arcode);
			}
			else {
				croak("panic dbd_phs_sv_complete: %s bad indp %d, arcode %d", phs->name, phs->indp, phs->arcode);
			}
		}
	}
}
void
dbd_phs_avsv_complete(imp_sth_t *imp_sth, phs_t *phs, I32 index, I32 debug)
{
	dTHX;
	AV *av = (AV*)SvRV(phs->sv);
	SV *sv = *av_fetch(av, index, 1);
	dbd_phs_sv_complete(imp_sth, phs, sv, 0);
	if (debug >= 2 || dbd_verbose >= 3 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
                      " dbd_phs_avsv_complete out '%s'[%ld] = %s (arcode %d, ind %d, len %d)\n",
		phs->name, (long)index, neatsvpv(sv,0), phs->arcode, phs->indp, phs->alen);
}


/* --- */


int
dbd_st_execute(SV *sth, imp_sth_t *imp_sth) /* <= -2:error, >=0:ok row count, (-1=unknown count) */
{
	dTHR;
	dTHX;
	ub4 row_count = 0;
	int debug 	  = DBIc_DBISTATE(imp_sth)->debug;
	int outparams = (imp_sth->out_params_av) ? AvFILL(imp_sth->out_params_av)+1 : 0;
	D_imp_dbh_from_sth;
	sword status;
	int is_select = (imp_sth->stmt_type == OCI_STMT_SELECT);


	if (debug >= 2 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "   dbd_st_execute %s (out%d, lob%d)...\n",
            oci_stmt_type_name(imp_sth->stmt_type), outparams, imp_sth->has_lobs);

	/* Don't attempt execute for nested cursor. It would be meaningless,
		and Oracle code has been seen to core dump */
	if (imp_sth->nested_cursor) {
		oci_error(sth, NULL, OCI_ERROR,
			"explicit execute forbidden for nested cursor");
		return -2;
	}


	if (outparams) {	/* check validity of bind_param_inout SV's	*/
		int i = outparams;
		while(--i >= 0) {
			phs_t *phs = (phs_t*)(void*)SvPVX(AvARRAY(imp_sth->out_params_av)[i]);
			SV *sv = phs->sv;
		/* Make sure we have the value in string format. Typically a number	*/
		/* will be converted back into a string using the same bound buffer	*/
		/* so the progv test below will not trip.			*/

		/* is the value a null? */
			phs->indp = (SvOK(sv)) ? 0 : -1;

			if (phs->out_prepost_exec) {
				if (!phs->out_prepost_exec(sth, imp_sth, phs, 1))
					return -2; /* out_prepost_exec already called ora_error()	*/
			}
			else
			if (SvTYPE(sv) == SVt_RV && SvTYPE(SvRV(sv)) == SVt_PVAV) {
				if (debug >= 2 || dbd_verbose >= 3 )
					PerlIO_printf(
                        DBIc_LOGPIO(imp_sth),
                        "	  with %s = [] (len %ld/%ld, indp %d, otype %d, ptype %d)\n",
                        phs->name,
                        (long)phs->alen, (long)phs->maxlen, phs->indp,
                        phs->ftype, (int)SvTYPE(sv));
				av_clear((AV*)SvRV(sv));
			}
			else
		/* Some checks for mutated storage since we pointed oracle at it.	*/
			if (SvTYPE(sv) != phs->sv_type
				|| (SvOK(sv) && !SvPOK(sv))
			/* SvROK==!SvPOK so cursor (SQLT_CUR) handle will call dbd_rebind_ph */
			/* that suits us for now */
				|| SvPVX(sv) != phs->progv
				|| (SvPOK(sv) && SvCUR(sv) > UB2MAXVAL)
			) {
				if (!dbd_rebind_ph(sth, imp_sth, phs))
					croak("Can't rebind placeholder %s", phs->name);
				}
				else {
					/* String may have grown or shrunk since it was bound	*/
					/* so tell Oracle about it's current length		*/
					ub2 prev_alen = phs->alen;
					phs->alen = (SvOK(sv)) ? SvCUR(sv) + phs->alen_incnull : 0+phs->alen_incnull;
					if (debug >= 2 || dbd_verbose >= 3 )
						PerlIO_printf(
                            DBIc_LOGPIO(imp_sth),
                            "	  with %s = '%.*s' (len %ld(%ld)/%ld, indp %d, "
                            "otype %d, ptype %d)\n",
							phs->name, (int)phs->alen,
                            (phs->indp == -1) ? "" : SvPVX(sv),
                            (long)phs->alen, (long)prev_alen,
                            (long)phs->maxlen, phs->indp,
                            phs->ftype, (int)SvTYPE(sv));
				}
			}
		}


		if (DBIc_has(imp_dbh,DBIcf_AutoCommit) && !is_select) {
			imp_sth->exe_mode=OCI_COMMIT_ON_SUCCESS;
			/* we don't AutoCommit on select so LOB locators work */
		} else if(imp_sth->exe_mode!=OCI_STMT_SCROLLABLE_READONLY){

			imp_sth->exe_mode=OCI_DEFAULT;
		}


		if (debug >= 2 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "Statement Execute Mode is %d (%s)\n",
                imp_sth->exe_mode,oci_exe_mode(imp_sth->exe_mode));

		OCIStmtExecute_log_stat(imp_sth, imp_sth->svchp, imp_sth->stmhp, imp_sth->errhp,
					(ub4)(is_select ? 0: 1),
					0, 0, 0,(ub4)imp_sth->exe_mode,status);


		if (status != OCI_SUCCESS) { /* may be OCI_ERROR or OCI_SUCCESS_WITH_INFO etc */
			/* we record the error even for OCI_SUCCESS_WITH_INFO */
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIStmtExecute"));
			/* but only bail out here if not OCI_SUCCESS_WITH_INFO */
			if (status != OCI_SUCCESS_WITH_INFO)
				return -2;
		}

	if (is_select) {
		DBIc_ACTIVE_on(imp_sth);
		DBIc_ROW_COUNT(imp_sth) = 0; /* reset (possibly re-exec'ing) */
		row_count = 0;
		/*reinit the rs_array as well
		  as we may have more than one exe on a prepare*/
		rs_array_init(imp_sth);
	}
	else {
		OCIAttrGet_stmhp_stat(imp_sth, &row_count, 0, OCI_ATTR_ROW_COUNT, status);
	}

	if (debug >= 2 || dbd_verbose >= 3 ) {
		ub2 sqlfncode;
		OCIAttrGet_stmhp_stat(imp_sth, &sqlfncode, 0, OCI_ATTR_SQLFNCODE, status);
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
			"	dbd_st_execute %s returned (%s, rpc%ld, fn%d, out%d)\n",
			oci_stmt_type_name(imp_sth->stmt_type),
			oci_status_name(status),
			(long)row_count, sqlfncode, imp_sth->has_inout_params);
	}

	if (is_select && !imp_sth->done_desc) {
	/* describe and allocate storage for results (if any needed)	*/
		if (!dbd_describe(sth, imp_sth))
			return -2; /* dbd_describe already called oci_error()	*/
	}

	if (imp_sth->has_lobs && imp_sth->stmt_type != OCI_STMT_SELECT) {
		if (!post_execute_lobs(sth, imp_sth, row_count))
			return -2; /* post_insert_lobs already called oci_error()	*/
	}

	if (outparams) {	/* check validity of bound output SV's	*/
		int i = outparams;
		while(--i >= 0) {
			/* phs->alen has been updated by Oracle to hold the length of the result */
			phs_t *phs = (phs_t*)(void*)SvPVX(AvARRAY(imp_sth->out_params_av)[i]);
			SV *sv = phs->sv;
			if (debug >= 2 || dbd_verbose >= 3 ) {
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
					"dbd_st_execute(): Analyzing inout  a parameter '%s"
                    " of type=%d  name=%s'\n",
					phs->name,phs->ftype,sql_typecode_name(phs->ftype));
			}
			if( phs->ftype == ORA_VARCHAR2_TABLE ){
				dbd_phs_varchar_table_posy_exe(imp_sth, phs);
				continue;
			}
			if( phs->ftype == ORA_NUMBER_TABLE ){
				dbd_phs_number_table_post_exe(imp_sth, phs);
				continue;
			}

			if (phs->out_prepost_exec) {
				if (!phs->out_prepost_exec(sth, imp_sth, phs, 0))
					return -2; /* out_prepost_exec already called ora_error()	*/
			 }
			  else {
				if (SvTYPE(sv) == SVt_RV && SvTYPE(SvRV(sv)) == SVt_PVAV) {
					AV *av = (AV*)SvRV(sv);
					I32 avlen = AvFILL(av);
					if (avlen >= 0)
                        dbd_phs_avsv_complete(imp_sth, phs, avlen, debug);
				}
				else {
					dbd_phs_sv_complete(imp_sth, phs, sv, debug);
				}
			}
		}
	}

	return row_count;	/* row count (0 will be returned as "0E0")	*/
}

static int
do_bind_array_exec(sth, imp_sth, phs,utf8,parma_index,tuples_utf8_av,tuples_status_av)
	SV *sth;
	imp_sth_t *imp_sth;
	phs_t *phs;
	int utf8;
	AV *tuples_utf8_av,*tuples_status_av;
	int parma_index;
	{
	dTHX;
	sword status;
	ub1 csform;
	ub2 csid;
	int trace_level = DBIc_DBISTATE(imp_sth)->debug;
	int i;
	OCIBindByName_log_stat(imp_sth, imp_sth->stmhp, &phs->bndhp, imp_sth->errhp,
			(text*)phs->name, (sb4)strlen(phs->name),
			0,
			(sb4)phs->maxlen,
			(ub2)phs->ftype, 0,
			NULL, /* ub2 *alen_ptr not needed with OCIBindDynamic */
			0,
			0,	  /* max elements that can fit in allocated array */
			NULL, /* (ptr to) current number of elements in array */
			(ub4)OCI_DATA_AT_EXEC,
			status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindByName");
		return 0;
	}


	OCIBindDynamic_log(imp_sth, phs->bndhp, imp_sth->errhp,
					(dvoid *)phs, dbd_phs_in,
					(dvoid *)phs, dbd_phs_out, status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIBindDynamic");
		return 0;
	}
	/* copied and adapted from dbd_rebind_ph */

	csform = phs->csform;

	if (!csform && (utf8 & ARRAY_BIND_UTF8)) {
		/* try to default csform to avoid translation through non-unicode */
		if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT))				/* prefer IMPLICIT */
			csform = SQLCS_IMPLICIT;
		else if (CSFORM_IMPLIES_UTF8(SQLCS_NCHAR))
			csform = SQLCS_NCHAR;   /* else leave csform == 0 */
		if (trace_level || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "do_bind_array_exec() (2): rebinding %s with UTF8 value %s", phs->name,
				(csform == SQLCS_IMPLICIT) ? "so setting csform=SQLCS_IMPLICIT" :
				(csform == SQLCS_NCHAR)	? "so setting csform=SQLCS_NCHAR" :
				 "but neither CHAR nor NCHAR are unicode\n");
	}

	if (csform) {
		/* set OCI_ATTR_CHARSET_FORM before we get the default OCI_ATTR_CHARSET_ID */
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
			&csform, (ub4) 0, (ub4) OCI_ATTR_CHARSET_FORM, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_FORM)"));
			return 0;
		}
	}

	if (!phs->csid_orig) {	  /* get the default csid Oracle would use */
		OCIAttrGet_log_stat(imp_sth, phs->bndhp, OCI_HTYPE_BIND, &phs->csid_orig, NULL,
			OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
	}

	/* if app has specified a csid then use that, else use default */
	csid = (phs->csid) ? phs->csid : phs->csid_orig;
	/* if data is utf8 but charset isn't then switch to utf8 csid if possible */
	if ((utf8 & ARRAY_BIND_UTF8) && !CS_IS_UTF8(csid)) {
		/* if the specified or default csid is not utf8 _compatible_ AND we have
		* mixed utf8 and native (non-utf8) data, then it's a fatal problem
		* utf8 _compatible_ means, can be upgraded to utf8, ie. utf8 or ascii */
		if ((utf8 & ARRAY_BIND_NATIVE) && CS_IS_NOT_UTF8_COMPATIBLE(csid)) {
				oratext  charsetname[OCI_NLS_MAXBUFSZ];
				OCINlsCharSetIdToName(imp_sth->envhp,charsetname, sizeof(charsetname),csid );

				for(i=0;i<av_len(tuples_utf8_av)+1;i++){
					SV *err_svs[3];
					SV *item;
					item=*(av_fetch(tuples_utf8_av,i,0));
					err_svs[0] = newSViv((IV)0);
					err_svs[1] = newSVpvf("DBD Oracle Warning: You have mixed utf8 and non-utf8 in an array bind in parameter#%d. This may result in corrupt data. The Query charset id=%d, name=%s",parma_index+1,csid,charsetname);
					err_svs[2] = newSVpvn("S1000", 0);
					av_store(tuples_status_av,SvIV(item),newRV_noinc((SV *)(av_make(3, err_svs))));
				}



		}
		csid = utf8_csid; /* not al32utf8_csid here on purpose */
	}

	if (trace_level >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "do_bind_array_exec(): bind %s <== [array of values] "
			"(%s, %s, csid %d->%d->%d, ftype %d (%s), csform %d (%s)->%d (%s)"
            ", maxlen %lu, maxdata_size %lu)\n",
			phs->name,
			(phs->is_inout) ? "inout" : "in",
			(utf8 ? "is-utf8" : "not-utf8"),
			phs->csid_orig, phs->csid, csid,
			phs->ftype, sql_typecode_name(phs->ftype),
            phs->csform,oci_csform_name(phs->csform), csform,oci_csform_name(csform),
			(unsigned long)phs->maxlen, (unsigned long)phs->maxdata_size);

	if (csid) {
		OCIAttrSet_log_stat(imp_sth, phs->bndhp, (ub4) OCI_HTYPE_BIND,
			&csid, (ub4) 0, (ub4) OCI_ATTR_CHARSET_ID, imp_sth->errhp, status);
		if ( status != OCI_SUCCESS ) {
			oci_error(sth, imp_sth->errhp, status, ora_sql_error(imp_sth,"OCIAttrSet (OCI_ATTR_CHARSET_ID)"));
			return 0;
		}
	}

	return 1;
}

static void
init_bind_for_array_exec(phs)
	phs_t *phs;
{
	dTHX;
	if (phs->sv == &PL_sv_undef) { /* first bind for this placeholder  */
		phs->is_inout = 0;
		phs->maxlen = 1;
		/* treat Oracle7 SQLT_CUR as SQLT_RSET for Oracle8 */
		if (phs->ftype==102)
			phs->ftype = ORA_RSET;
		/* some types require the trailing null included in the length. */
		/* SQLT_STR=5=STRING, SQLT_AVC=97=VARCHAR */
		phs->alen_incnull = (phs->ftype==SQLT_STR || phs->ftype==SQLT_AVC);
	}
}

int
ora_st_execute_array(sth, imp_sth, tuples, tuples_status, columns, exe_count, err_count)
	SV *sth;
	imp_sth_t *imp_sth;
	SV *tuples;
	SV *tuples_status;
	SV *columns;
	ub4 exe_count;
	SV *err_count;
{
	dTHX;
	dTHR;
	ub4 row_count = 0;
	int debug = DBIc_DBISTATE(imp_sth)->debug;
	D_imp_dbh_from_sth;
	sword status, exe_status;
	int is_select = (imp_sth->stmt_type == OCI_STMT_SELECT);
	AV *tuples_av, *tuples_status_av, *columns_av,*tuples_utf8_av;
	ub4 oci_mode;
	ub4 num_errs;
	int i,j;
	int autocommit = DBIc_has(imp_dbh,DBIcf_AutoCommit);
	SV **sv_p;
	phs_t **phs;
	SV *sv;
	AV *av;
	int param_count;
	char namebuf[30];
	STRLEN len;
	int outparams = (imp_sth->out_params_av) ? AvFILL(imp_sth->out_params_av)+1 : 0;
	int *utf8_flgs;
	tuples_utf8_av = newAV();
	sv_2mortal((SV*)tuples_utf8_av);

	if (debug >= 2 || dbd_verbose >= 3 )
		PerlIO_printf(
			DBIc_LOGPIO(imp_sth),
			"  ora_st_execute_array %s count=%d (%s %s %s)...\n",
			oci_stmt_type_name(imp_sth->stmt_type), exe_count,
			neatsvpv(tuples,0), neatsvpv(tuples_status,0),
			neatsvpv(columns, 0));

	if (is_select) {
		croak("ora_st_execute_array(): SELECT statement not supported "
			"for array operation.");
	}

	if (imp_sth->has_lobs) {
		croak("ora_st_execute_array(): LOBs not "
			"supported for array operation.");
	}

	/* Check that the `tuples' parameter is an array ref, find the length,
		and store it in the statement handle for the OCI callback. */
	if(!SvROK(tuples) || SvTYPE(SvRV(tuples)) != SVt_PVAV) {
		croak("ora_st_execute_array(): Not an array reference.");
	}
	tuples_av = (AV*)SvRV(tuples);

	/* Check the `columns' parameter. */
	if(SvTRUE(columns)) {
		if(!SvROK(columns) || SvTYPE(SvRV(columns)) != SVt_PVAV) {
		  croak("ora_st_execute_array(): columns not an array peference.");
		}
		columns_av = (AV*)SvRV(columns);
	} else {
		columns_av = NULL;
	}
	/* Check the `tuples_status' parameter. */
	if(SvTRUE(tuples_status)) {
		if(!SvROK(tuples_status) || SvTYPE(SvRV(tuples_status)) != SVt_PVAV) {
		  	croak("ora_st_execute_array(): tuples_status not an array reference.");
		}
		tuples_status_av = (AV*)SvRV(tuples_status);
		av_fill(tuples_status_av, exe_count - 1);

	} else {
		tuples_status_av = NULL;
	}

	/* Nothing to do if no tuples. */
	if(exe_count <= 0)
	  return 0;

	/* Ensure proper OCIBindByName() calls for all placeholders.
	if(!ora_st_bind_for_array_exec(sth, imp_sth, tuples_av, exe_count,
								   DBIc_NUM_PARAMS(imp_sth), columns_av))
		return -2;

	fix for Perl undefined warning. Moved out of function back out to main code
	Still ensures proper OCIBindByName*/

	param_count=DBIc_NUM_PARAMS(imp_sth);
	Newz(0,phs,param_count*sizeof(*phs),phs_t *);
	Newz(0,utf8_flgs,param_count*sizeof(int),int);

	for(j = 0; (unsigned int) j < exe_count; j++) {
		/* Fill in 'unknown' exe count in every element (know not how to get
			individual execute row counts from OCI).
			Moved it here as there is no need to iterate twice over it
			this should speed it up somewhat for large binds*/

		if (SvTRUE(tuples_status)){
			av_store(tuples_status_av, j, newSViv((IV)-1));
		}
		sv_p = av_fetch(tuples_av, j, 0);
		if(sv_p == NULL) {
			Safefree(phs);
			Safefree(utf8_flgs);
			croak("Cannot fetch tuple %d", j);
		}
		sv = *sv_p;
		if(!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV) {
			Safefree(phs);
			Safefree(utf8_flgs);
			croak("Not an array ref in element %d", j);
		}
		av = (AV*)SvRV(sv);
		for(i = 0; i < param_count; i++) {
			if(!phs[i]) {
				SV **phs_svp;
				sprintf(namebuf, ":p%d", i+1);
				phs_svp = hv_fetch(imp_sth->all_params_hv,
							namebuf, strlen(namebuf), 0);
				if (phs_svp == NULL) {
					Safefree(utf8_flgs);
					Safefree(phs);
					croak("Can't execute for non-existent placeholder :%d", i);
				}
				phs[i] = (phs_t*)(void*)SvPVX(*phs_svp); /* placeholder struct */
				if(phs[i]->idx < 0) {
					Safefree(phs);
					croak("Placeholder %d not of ?/:1 type", i);
				}
				init_bind_for_array_exec(phs[i]);
			}
			sv_p = av_fetch(av, phs[i]->idx, 0);
			if(sv_p == NULL) {
				Safefree(utf8_flgs);
				Safefree(phs);
				croak("Cannot fetch value for param %d in entry %d", i, j);
			}

			sv = *sv_p;

			/*check to see if value sv is a null (undef) if it is upgrade it*/
 			if (!SvOK(sv))	{
				(void)SvUPGRADE(sv, SVt_PV);
				len = 0;
			}
			else {
				SvPV(sv, len);
			}


			/* Find the value length, and increase maxlen if needed. */
			if(SvROK(sv)) {
				Safefree(phs);
				Safefree(utf8_flgs);
				croak("Can't bind a reference (%s) for param %d, entry %d",
				neatsvpv(sv,0), i, j);
			}
			if(len > (unsigned int) phs[i]->maxlen)
				phs[i]->maxlen = len;

			/* update the utf8_flgs for this value */
			if (SvUTF8(sv)) {
				utf8_flgs[i] |= ARRAY_BIND_UTF8;
				if (SvTRUE(tuples_status)){
					av_push(tuples_utf8_av,newSViv(j));
				}


			}
			else {
				utf8_flgs[i] |= ARRAY_BIND_NATIVE;

			}
			/* Do OCI bind calls on last iteration. */
			if( ((unsigned int) j ) == exe_count - 1 ) {
				do_bind_array_exec(sth, imp_sth, phs[i], utf8_flgs[i],i,tuples_utf8_av,tuples_status_av);
			}
		}
	}
	/* Store array of bind typles, for use in OCIBindDynamic() callback. */
	imp_sth->bind_tuples = tuples_av;
	imp_sth->rowwise = (columns_av == NULL);

	oci_mode = OCI_BATCH_ERRORS;
	if(autocommit)
		oci_mode |= OCI_COMMIT_ON_SUCCESS;

	OCIStmtExecute_log_stat(imp_sth, imp_sth->svchp, imp_sth->stmhp, imp_sth->errhp,
							exe_count, 0, 0, 0, oci_mode, exe_status);

	OCIAttrGet_stmhp_stat(imp_sth, &row_count, 0, OCI_ATTR_ROW_COUNT, status);


	 imp_sth->bind_tuples = NULL;

	if (exe_status != OCI_SUCCESS) {
 		oci_error(sth, imp_sth->errhp, exe_status, ora_sql_error(imp_sth,"OCIStmtExecute"));
		if(exe_status != OCI_SUCCESS_WITH_INFO)
			return -2;
	}
	if (outparams){
		i=outparams;
		while(--i >= 0) {
			phs_t *phs = (phs_t*)(void*)SvPVX(AvARRAY(imp_sth->out_params_av)[i]);
			SV *sv = phs->sv;
			if (SvTYPE(sv) == SVt_RV && SvTYPE(SvRV(sv)) == SVt_PVAV) {
				AV *av = (AV*)SvRV(sv);
				I32 avlen = AvFILL(av);
				for (j=0;j<=avlen;j++){
					dbd_phs_avsv_complete(imp_sth, phs, j, debug);
				}
			}
		}
	}

	OCIAttrGet_stmhp_stat(imp_sth, &num_errs, 0, OCI_ATTR_NUM_DML_ERRORS, status);

	if (debug >= 6 || dbd_verbose >= 6 )
		 PerlIO_printf(
             DBIc_LOGPIO(imp_sth),
             "	ora_st_execute_array %d errors in batch.\n",
             num_errs);
    if (num_errs) {
        sv_setiv(err_count,num_errs);
    }

	if(num_errs && tuples_status_av) {
		OCIError *row_errhp, *tmp_errhp;
		ub4 row_off;
		SV *err_svs[3];
		/*AV *err_av;*/
		sb4 err_code;

		err_svs[0] = newSViv((IV)0);
		err_svs[1] = newSVpvn("", 0);
		err_svs[2] = newSVpvn("S1000",5);
		OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &row_errhp, OCI_HTYPE_ERROR, status);
		OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &tmp_errhp, OCI_HTYPE_ERROR, status);
		for(i = 0; (unsigned int) i < num_errs; i++) {
			OCIParamGet_log_stat(imp_sth, imp_sth->errhp, OCI_HTYPE_ERROR,
								 tmp_errhp, (dvoid *)&row_errhp,
								 (ub4)i, status);
			OCIAttrGet_log_stat(imp_sth, row_errhp, OCI_HTYPE_ERROR, &row_off, 0,
								OCI_ATTR_DML_ROW_OFFSET, imp_sth->errhp, status);
			if (debug >= 6 || dbd_verbose >= 6 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	ora_st_execute_array error in row %d.\n",
                    row_off);
			sv_setpv(err_svs[1], "");
			err_code = oci_error_get((imp_xxh_t *)imp_sth, row_errhp, exe_status, NULL, err_svs[1], debug);
			sv_setiv(err_svs[0], (IV)err_code);
			av_store(tuples_status_av, row_off,
					 newRV_noinc((SV *)(av_make(3, err_svs))));
		}
		OCIHandleFree_log_stat(imp_sth, tmp_errhp, OCI_HTYPE_ERROR,  status);
		OCIHandleFree_log_stat(imp_sth, row_errhp, OCI_HTYPE_ERROR,  status);

		/* Do a commit here if autocommit is set, since Oracle
			doesn't do that for us when some rows are in error. */
		if(autocommit) {
			OCITransCommit_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp,
									OCI_DEFAULT, status);
			if (status != OCI_SUCCESS) {
				oci_error(sth, imp_sth->errhp, status, "OCITransCommit");
				return -2;
			}
		}
	}

	if(num_errs) {
		return -2;
	} else {

		return row_count;
	}
}




int
dbd_st_blob_read(SV *sth, imp_sth_t *imp_sth, int field, long offset, long len, SV *destrv, long destoffset)
{
	dTHX;
	ub4 retl = 0;
	SV *bufsv;
	imp_fbh_t *fbh = &imp_sth->fbh[field];
	int ftype = fbh->ftype;

	bufsv = SvRV(destrv);
	sv_setpvn(bufsv,"",0);	/* ensure it's writable string	*/

#ifdef UTF8_SUPPORT
	if (ftype == 112 && CS_IS_UTF8(ncharsetid) ) {
	  return ora_blob_read_mb_piece(sth, imp_sth, fbh, bufsv,
					offset, len, destoffset);
	}
#endif /* UTF8_SUPPORT */

	SvGROW(bufsv, (STRLEN)destoffset+len+1); /* SvGROW doesn't do +1	*/

	retl = ora_blob_read_piece(sth, imp_sth, fbh, bufsv,
				 offset, len, destoffset);
	if (!SvOK(bufsv)) { /* ora_blob_read_piece recorded error */
		ora_free_templob(sth, imp_sth, (OCILobLocator*)fbh->desc_h);
	return 0;
	}
	(void)ftype;	/* no unused */

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth),
		"	blob_read field %d+1, ftype %d, offset %ld, len %ld, "
        "destoffset %ld, retlen %ld\n",
		field, imp_sth->fbh[field].ftype, offset, len, destoffset, (long)retl);

	SvCUR_set(bufsv, destoffset+retl);

	*SvEND(bufsv) = '\0'; /* consistent with perl sv_setpvn etc	*/

	return 1;
}


int
dbd_st_rows(SV *sth, imp_sth_t *imp_sth)
{
	dTHX;
	ub4 row_count = 0;
	sword status;
	OCIAttrGet_stmhp_stat(imp_sth, &row_count, 0, OCI_ATTR_ROW_COUNT, status);
	if (status != OCI_SUCCESS) {
	oci_error(sth, imp_sth->errhp, status, "OCIAttrGet OCI_ATTR_ROW_COUNT");
	return -1;
	}
	return row_count;
}


int
dbd_st_finish(SV *sth, imp_sth_t *imp_sth)
{
	dTHR;
	dTHX;
	D_imp_dbh_from_sth;
	sword status;
	int num_fields = DBIc_NUM_FIELDS(imp_sth);
	int i;


	if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth), "	dbd_st_finish\n");

	if (!DBIc_ACTIVE(imp_sth))
		return 1;

	/* Cancel further fetches from this cursor.				 */
	/* We don't close the cursor till DESTROY (dbd_st_destroy). */
	/* The application may re execute(...) it.				  */

	/* Turn off ACTIVE here regardless of errors below.		*/
	DBIc_ACTIVE_off(imp_sth);

	for(i=0; i < num_fields; ++i) {
 		imp_fbh_t *fbh = &imp_sth->fbh[i];
		if (fbh->fetch_cleanup) fbh->fetch_cleanup(sth, fbh);
	}

	if (PL_dirty)			/* don't walk on the wild side	*/
		return 1;

	if (!DBIc_ACTIVE(imp_dbh))		/* no longer connected	*/
		return 1;

	/*fetching on a cursor with row =0 will explicitly free any
	server side resources this is what the next statment does,
	not sure if we need this for non scrolling cursors they should die on
	a OER(1403) no records)*/

	OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp, 0,
		OCI_FETCH_NEXT,0,  status);

	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		oci_error(sth, imp_sth->errhp, status, "Finish OCIStmtFetch");
		return 0;
	}
	return 1;
}


void
ora_free_fbh_contents(SV *sth, imp_fbh_t *fbh)
{
	dTHX;
    D_imp_sth(sth);
    D_imp_dbh_from_sth;

	if (fbh->fb_ary)
	fb_ary_free(fbh->fb_ary);
	sv_free(fbh->name_sv);

    /* see rt 75163 */
	if (fbh->desc_h) {
        boolean is_open;
        sword status;

        OCILobFileIsOpen_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, fbh->desc_h, &is_open, status);
        if (status == OCI_SUCCESS && is_open) {
            OCILobFileClose_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp,
                                     fbh->desc_h, status);
        }


        OCIDescriptorFree_log(imp_sth, fbh->desc_h, fbh->desc_t);
    }

	if (fbh->obj) {
		if (fbh->obj->obj_value)
			OCIObjectFree(fbh->imp_sth->envhp, fbh->imp_sth->errhp, fbh->obj->obj_value, (ub2)0);
		Safefree(fbh->obj);
	}

}

void
ora_free_phs_contents(imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	if (phs->desc_h)
        OCIDescriptorFree_log(imp_sth, phs->desc_h, phs->desc_t);
	if( phs->array_buf ){
		free(phs->array_buf);
		phs->array_buf=NULL;
	}
	if( phs->array_indicators ){
		free(phs->array_indicators);
		phs->array_indicators=NULL;
	}
	if( phs->array_lengths ){
		free(phs->array_lengths);
		phs->array_lengths=NULL;
	}

	phs->array_buflen=0;
	phs->array_numallocated=0;
	sv_free(phs->ora_field);
	sv_free(phs->sv);
}

void
ora_free_templob(SV *sth, imp_sth_t *imp_sth, OCILobLocator *lobloc)
{
	dTHX;
#if defined(OCI_HTYPE_DIRPATH_FN_CTX)	/* >= 9.0 */
	boolean is_temporary = 0;
	sword status;
	OCILobIsTemporary_log_stat(imp_sth, imp_sth->envhp, imp_sth->errhp, lobloc, &is_temporary, status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobIsTemporary");
		return;
	}

	if (is_temporary) {
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 ) {
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	   OCILobFreeTemporary %s\n", oci_status_name(status));
		}
		OCILobFreeTemporary_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobloc, status);
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobFreeTemporary");
			return;
		}
	}
#endif
}


void
dbd_st_destroy(SV *sth, imp_sth_t *imp_sth)
{
	int fields;
	int i;
	sword status;
	dTHX ;
	D_imp_dbh_from_sth;

	/*  Don't free the OCI statement handle for a nested cursor. It will
		be reused by Oracle on the next fetch. Indeed, we never
		free these handles. Experiment shows that Oracle frees them
		when they are no longer needed.
	*/
	/* get rid of describe handle if used*/

	/* if we are using a scrolling cursor we should get rid of the
	cursor by fetching row 0 */
	if (imp_sth->exe_mode==OCI_STMT_SCROLLABLE_READONLY && DBIc_ACTIVE(imp_dbh)) {
		OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp, 0,OCI_FETCH_NEXT,0,  status);
	}

	if (imp_sth->dschp){
		OCIHandleFree_log_stat(imp_sth, imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
	}


	if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth), "	dbd_st_destroy %s\n",
		(PL_dirty) ? "(OCIHandleFree skipped during global destruction)" :
		(imp_sth->nested_cursor) ?"(OCIHandleFree skipped for nested cursor)" : "");

	if (!PL_dirty) { /* XXX not ideal, leak may be a problem in some cases */
		if (!imp_sth->nested_cursor) {
			OCIHandleFree_log_stat(imp_sth, imp_sth->stmhp, OCI_HTYPE_STMT, status);
			if (status != OCI_SUCCESS)
				oci_error(sth, imp_sth->errhp, status, "OCIHandleFree");
		}
	}

	/* Free off contents of imp_sth	*/

	if (imp_sth->lob_refetch)
		ora_free_lob_refetch(sth, imp_sth);

	fields = DBIc_NUM_FIELDS(imp_sth);
	imp_sth->in_cache  = 0;
	imp_sth->eod_errno = 1403;
	for(i=0; i < fields; ++i) {
		imp_fbh_t *fbh = &imp_sth->fbh[i];
		ora_free_fbh_contents(sth, fbh);
	}
	Safefree(imp_sth->fbh);
	if (imp_sth->fbh_cbuf)
		Safefree(imp_sth->fbh_cbuf);
	Safefree(imp_sth->statement);

	if (imp_sth->out_params_av)
		sv_free((SV*)imp_sth->out_params_av);

	if (imp_sth->all_params_hv) {
		HV *hv = imp_sth->all_params_hv;
		SV *sv;
		char *key;
		I32 retlen;
		hv_iterinit(hv);
		while( (sv = hv_iternextsv(hv, &key, &retlen)) != NULL ) {
			if (sv != &PL_sv_undef) {
			  	phs_t *phs = (phs_t*)(void*)SvPVX(sv);
				if (phs->desc_h && phs->desc_t == OCI_DTYPE_LOB)
					ora_free_templob(sth, imp_sth, (OCILobLocator*)phs->desc_h);
		  		ora_free_phs_contents(imp_sth, phs);
			}
		}
		sv_free((SV*)imp_sth->all_params_hv);
	}

	DBIc_IMPSET_off(imp_sth);		/* let DBI know we've done it	*/

}


int
dbd_st_STORE_attrib(SV *sth, imp_sth_t *imp_sth, SV *keysv, SV *valuesv)
{
	dTHX;
	STRLEN kl;
	SV *cachesv = NULL;
	char *key = SvPV(keysv,kl);
	if( imp_sth ) { /* For GCC not to warn on unused argument */}
/*	int on = SvTRUE(valuesv);
	int oraperl = DBIc_COMPAT(imp_sth); */
	if (strEQ(key, "ora_fetchtest")) {
		ora_fetchtest = SvIV(valuesv);
	}
	else
		return FALSE;

	if (cachesv) /* cache value for later DBI 'quick' fetch? */
		(void)hv_store((HV*)SvRV(sth), key, kl, cachesv, 0);
	return TRUE;
}


SV *
dbd_st_FETCH_attrib(SV *sth, imp_sth_t *imp_sth, SV *keysv)
{
	dTHX;
	STRLEN kl;
	char *key = SvPV(keysv,kl);
	int i;
	SV *retsv = NULL;
	/* Default to caching results for DBI dispatch quick_FETCH	*/
	int cacheit = TRUE;
	/* int oraperl = DBIc_COMPAT(imp_sth); */

	if (kl==13 && strEQ(key, "NUM_OF_PARAMS"))	/* handled by DBI */
		return Nullsv;

	if (!imp_sth->done_desc && !dbd_describe(sth, imp_sth)) {
		STRLEN lna;
	/* dbd_describe has already called ora_error()		*/
	/* we can't return Nullsv here because the xs code will	*/
	/* then just pass the attribute name to DBI for FETCH.	*/
		croak("Describe failed during %s->FETCH(%s): %ld: %s",
			SvPV(sth,PL_na), key, (long)SvIV(DBIc_ERR(imp_sth)),
			SvPV(DBIc_ERRSTR(imp_sth),lna)
		);
	}

	i = DBIc_NUM_FIELDS(imp_sth);

	if (kl==4 && strEQ(key, "NAME")) {
		AV *av = newAV();
        SV *x;

		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0) {
            x = newSVpv((char*)imp_sth->fbh[i].name,0);
            if (CSFORM_IMPLIES_UTF8(SQLCS_IMPLICIT)) {
#ifdef sv_utf8_decode
                sv_utf8_decode(x);
#else
                SvUTF8_on(x);
#endif
            }
			av_store(av, i, x);
        }
	}
	else if (kl==11 && strEQ(key, "ParamValues")) {
		HV *pvhv = newHV();
		if (imp_sth->all_params_hv) {
			SV *sv;
			char *key;
			I32 keylen;
			hv_iterinit(imp_sth->all_params_hv);
			while ( (sv = hv_iternextsv(imp_sth->all_params_hv, &key, &keylen)) ) {
				phs_t *phs = (phs_t*)(void*)SvPVX(sv);	   /* placeholder struct   */
				(void)hv_store(pvhv, key, keylen, newSVsv(phs->sv), 0);
 			}
		}
		retsv = newRV_noinc((SV*)pvhv);
		cacheit = FALSE;

	}
	else if (kl==11 && strEQ(key, "ora_lengths")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv((IV)imp_sth->fbh[i].disize));
	}
	else if (kl==9 && strEQ(key, "ora_types")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv(imp_sth->fbh[i].dbtype));
	}
	else if (kl==4 && strEQ(key, "TYPE")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv(ora2sql_type(imp_sth->fbh+i).dbtype));
	}
	else if (kl==5 && strEQ(key, "SCALE")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv(ora2sql_type(imp_sth->fbh+i).scale));
	}
	else if (kl==9 && strEQ(key, "PRECISION")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv(ora2sql_type(imp_sth->fbh+i).prec));
#ifdef XXX
	}
	else if (kl==9 && strEQ(key, "ora_rowid")) {
		/* return current _binary_ ROWID (oratype 11) uncached	*/
		/* Use { ora_type => 11 } when binding to a placeholder	*/
		retsv = newSVpv((char*)&imp_sth->cda->rid, sizeof(imp_sth->cda->rid));
		cacheit = FALSE;
#endif
	}
	else if (kl==17 && strEQ(key, "ora_est_row_width")) {
		retsv = newSViv(imp_sth->est_width);
		cacheit = TRUE;
	}
	else if (kl==11 && strEQ(key, "RowsInCache")) {
		retsv = newSViv(imp_sth->RowsInCache);
		cacheit = FALSE;

	}else if (kl==12 && strEQ(key, "RowCacheSize")) {
		retsv = newSViv(imp_sth->RowCacheSize);
		cacheit = FALSE;
	}
	else if (kl==8 && strEQ(key, "NULLABLE")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, boolSV(imp_sth->fbh[i].nullok));
	}
	else if (kl==13 && strEQ(key, "len_char_size")) {
		AV *av = newAV();
		retsv = newRV(sv_2mortal((SV*)av));
		while(--i >= 0)
			av_store(av, i, newSViv(imp_sth->fbh[i].len_char_size));
	}
	else {
		return Nullsv;
	}
	if (cacheit) { /* cache for next time (via DBI quick_FETCH)	*/
		SV **svp = hv_fetch((HV*)SvRV(sth), key, kl, 1);
		sv_free(*svp);
		*svp = retsv;
		(void)SvREFCNT_inc(retsv);	/* so sv_2mortal won't free it	*/
	}
	return sv_2mortal(retsv);
}

/* --------------------------------------- */

static sql_fbh_t
ora2sql_type(imp_fbh_t* fbh) {
	sql_fbh_t sql_fbh;
	sql_fbh.dbtype	= fbh->dbtype;
	sql_fbh.prec	= fbh->prec;
	sql_fbh.scale	= fbh->scale;

	switch(fbh->dbtype) { /* oracle Internal (not external) types */
	case SQLT_NUM:
		if (fbh->scale == -127) { /* FLOAT, REAL, DOUBLE_PRECISION */
			sql_fbh.dbtype = SQL_DOUBLE;
			sql_fbh.scale  = 0; /* better: undef */
			if (fbh->prec == 0) { /* NUMBER; s. Oracle Bug# 2755842, 2235818 */
				sql_fbh.prec   = 126;
			}
		}
		else if (fbh->scale == 0) {
			if (fbh->prec == 0) { /* NUMBER */
				sql_fbh.dbtype = SQL_DOUBLE;
				sql_fbh.prec   = 126;
			}
			else { /* INTEGER, NUMBER(p,0) */
				sql_fbh.dbtype = SQL_DECIMAL; /* better: SQL_INTEGER */
			}
	}
		else { /* NUMBER(p,s) */
			sql_fbh.dbtype = SQL_DECIMAL; /* better: SQL_NUMERIC */
		}
		break;
#ifdef SQLT_IBDOUBLE
	case SQLT_BDOUBLE:
	case SQLT_BFLOAT:
	case SQLT_IBDOUBLE:
	case SQLT_IBFLOAT:
			sql_fbh.dbtype = SQL_DOUBLE;
			sql_fbh.prec   = 126;
			break;
#endif
	case SQLT_CHR:  sql_fbh.dbtype = SQL_VARCHAR;	   break;
	case SQLT_LNG:  sql_fbh.dbtype = SQL_LONGVARCHAR;   break; /* long */
	case SQLT_DAT:  sql_fbh.dbtype = SQL_TYPE_TIMESTAMP;break;
	case SQLT_BIN:  sql_fbh.dbtype = SQL_BINARY;		break; /* raw */
	case SQLT_LBI:  sql_fbh.dbtype = SQL_LONGVARBINARY; break; /* long raw */
	case SQLT_AFC:  sql_fbh.dbtype = SQL_CHAR;		  break; /* Ansi fixed char */
	case SQLT_CLOB: sql_fbh.dbtype = SQL_CLOB;		break;
	case SQLT_BLOB: sql_fbh.dbtype = SQL_BLOB;		break;
#ifdef SQLT_TIMESTAMP_TZ
	case SQLT_DATE:		sql_fbh.dbtype = SQL_DATE;			break;
	case SQLT_TIME:		sql_fbh.dbtype = SQL_TIME;			break;
	case SQLT_TIME_TZ:		sql_fbh.dbtype = SQL_TYPE_TIME_WITH_TIMEZONE;	break;
	case SQLT_TIMESTAMP:	sql_fbh.dbtype = SQL_TYPE_TIMESTAMP;		break;
	case SQLT_TIMESTAMP_TZ:	sql_fbh.dbtype = SQL_TYPE_TIMESTAMP_WITH_TIMEZONE; break;
	case SQLT_TIMESTAMP_LTZ:	sql_fbh.dbtype = SQL_TYPE_TIMESTAMP_WITH_TIMEZONE; break;
	case SQLT_INTERVAL_YM:	sql_fbh.dbtype = SQL_INTERVAL_YEAR_TO_MONTH;	break;
	case SQLT_INTERVAL_DS:	sql_fbh.dbtype = SQL_INTERVAL_DAY_TO_SECOND;	break;
#endif
	default:		sql_fbh.dbtype = -9000 - fbh->dbtype; /* else map type into DBI reserved standard range */
	}
	return sql_fbh;
}

static void
dump_env_to_trace(imp_dbh_t *imp_dbh) {
	dTHX;
	int i = 0;
	char *p;

#if defined (__APPLE__)
	#include <crt_externs.h>
	#define environ (*_NSGetEnviron())
#elif defined (__BORLANDC__)
	extern char **environ;
#endif


	PerlIO_printf(DBIc_LOGPIO(imp_dbh), "Environment variables:\n");
	do {
        p = (char*)environ[i++];
        PerlIO_printf(DBIc_LOGPIO(imp_dbh),"\t%s\n",p);
	} while ((char*)environ[i] != '\0');
}

static void disable_taf(
    imp_dbh_t *imp_dbh) {

    sword status;
    OCIFocbkStruct 	tafailover;

    tafailover.fo_ctx = NULL;
    tafailover.callback_function = NULL;
    OCIAttrSet_log_stat(imp_dbh, imp_dbh->srvhp, (ub4) OCI_HTYPE_SERVER,
                        (dvoid *) &tafailover, (ub4) 0,
                        (ub4) OCI_ATTR_FOCBK, imp_dbh->errhp, status);
    return;
}

static int enable_taf(
    SV *dbh,
    imp_dbh_t *imp_dbh) {

    boolean can_taf = 0;
    sword status;

#ifdef OCI_ATTR_TAF_ENABLED
    OCIAttrGet_log_stat(imp_dbh, imp_dbh->srvhp, OCI_HTYPE_SERVER, &can_taf, NULL,
                        OCI_ATTR_TAF_ENABLED, imp_dbh->errhp, status);
#endif

    if (!can_taf){
        croak("You are attempting to enable TAF on a server that is not TAF Enabled \n");
    }

	status = reg_taf_callback(dbh, imp_dbh);
    if (status != OCI_SUCCESS) {
        oci_error(dbh, NULL, status, "Setting TAF Callback Failed! ");
        return 0;
    }
    return 1;
}


