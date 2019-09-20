#include "Oracle.h"

#define BIND_PARAM_INOUT_ALLOW_ARRAY

DBISTATE_DECLARE;

MODULE = DBD::Oracle	PACKAGE = DBD::Oracle

I32
constant(name=Nullch)
	char *name
	ALIAS:
	ORA_VARCHAR2 = ORA_VARCHAR2
	ORA_NUMBER	 = ORA_NUMBER
	ORA_STRING	 = ORA_STRING
	ORA_LONG	 = ORA_LONG
	ORA_ROWID	 = ORA_ROWID
	ORA_DATE	 = ORA_DATE
	ORA_RAW	 	 = ORA_RAW
	ORA_LONGRAW	 = ORA_LONGRAW
	ORA_CHAR	 = ORA_CHAR
	ORA_CHARZ	 = ORA_CHARZ
	ORA_MLSLABEL = 105
	ORA_XMLTYPE	 = ORA_XMLTYPE
	ORA_CLOB	 = ORA_CLOB
	ORA_BLOB	 = ORA_BLOB
	ORA_RSET	 = ORA_RSET
	ORA_VARCHAR2_TABLE	= ORA_VARCHAR2_TABLE
	ORA_NUMBER_TABLE	= ORA_NUMBER_TABLE
	ORA_SYSDBA	 		= 0x0002
	ORA_SYSOPER	 		= 0x0004
	ORA_SYSASM	 		= 0x8000
	ORA_SYSBACKUP	 		= 0x00020000
	ORA_SYSDG	 		= 0x00040000
	ORA_SYSKM	 		= 0x00080000
	SQLCS_IMPLICIT 		= SQLCS_IMPLICIT
	SQLCS_NCHAR			= SQLCS_NCHAR
	SQLT_INT	 		= SQLT_INT
	SQLT_FLT	 		= SQLT_FLT
	OCI_BATCH_MODE		= 0x01
	OCI_EXACT_FETCH		= 0x02
	OCI_KEEP_FETCH_STATE	= 0x04
	OCI_DESCRIBE_ONLY		= 0x10
	OCI_COMMIT_ON_SUCCESS	= 0x20
	OCI_NON_BLOCKING		= 0x40
	OCI_BATCH_ERRORS		= 0x80
	OCI_PARSE_ONLY			= 0x100
	OCI_SHOW_DML_WARNINGS	= 0x400
  	OCI_FETCH_CURRENT 		= OCI_FETCH_CURRENT
	OCI_FETCH_NEXT 			= OCI_FETCH_NEXT
	OCI_FETCH_FIRST			= OCI_FETCH_FIRST
	OCI_FETCH_LAST 			= OCI_FETCH_LAST
	OCI_FETCH_PRIOR 		= OCI_FETCH_PRIOR
	OCI_FETCH_ABSOLUTE 		= OCI_FETCH_ABSOLUTE
	OCI_FETCH_RELATIVE		= OCI_FETCH_RELATIVE
	OCI_FO_END				= OCI_FO_END
	OCI_FO_ABORT			= OCI_FO_ABORT
	OCI_FO_REAUTH			= OCI_FO_REAUTH
	OCI_FO_BEGIN			= OCI_FO_BEGIN
	OCI_FO_ERROR			= OCI_FO_ERROR
	OCI_FO_NONE				= OCI_FO_NONE
	OCI_FO_SESSION			= OCI_FO_SESSION
	OCI_FO_SELECT			= OCI_FO_SELECT
	OCI_FO_TXNAL			= OCI_FO_TXNAL
	OCI_FO_RETRY			= OCI_FO_RETRY
	OCI_STMT_SCROLLABLE_READONLY 	= 0x08
	OCI_PRELIM_AUTH 		= 0x00000008
	OCI_DBSTARTUPFLAG_FORCE 	= 0x00000001
	OCI_DBSTARTUPFLAG_RESTRICT 	= 0x00000002
	OCI_DBSHUTDOWN_TRANSACTIONAL	 = 1
	OCI_DBSHUTDOWN_TRANSACTIONAL_LOCAL = 2
	OCI_DBSHUTDOWN_IMMEDIATE = 3
	OCI_DBSHUTDOWN_ABORT 	= 4
	OCI_DBSHUTDOWN_FINAL 	= 5
	SQLT_CHR	= SQLT_CHR
	SQLT_BIN	= SQLT_BIN
	CODE:
	if (!ix) {
	if (!name) name = GvNAME(CvGV(cv));
	croak("Unknown DBD::Oracle constant '%s'", name);
	}
	else RETVAL = ix;
	OUTPUT:
	RETVAL


void
ORA_OCI()
	CODE:
	SV *sv = sv_newmortal();
	sv_setnv(sv, atof(ORA_OCI_VERSION));	/* 9.1! see docs */
	sv_setpv(sv, ORA_OCI_VERSION);		/* 9.10.11.12	*/
	SvNOK_on(sv); /* dualvar hack */
	ST(0) = sv;

void
ora_env_var(name)
	char *name
	CODE:
	char buf[1024];
	char *p = ora_env_var(name, buf, sizeof(buf)-1);
	SV *sv = sv_newmortal();
	if (p)
		sv_setpv(sv, p);
	ST(0) = sv;

#if defined(__CYGWIN32__) || defined(__CYGWIN64__)
void
ora_cygwin_set_env(name, value)
	char * name
	char * value
	CODE:
	ora_cygwin_set_env(name, value);

#endif /* __CYGWIN32__ */

INCLUDE: Oracle.xsi



# ------------------------------------------------------------
# statement interface
# ------------------------------------------------------------
MODULE = DBD::Oracle	PACKAGE = DBD::Oracle::st


void ora_stmt_type(sth)
 SV *	sth
	PREINIT:
	D_imp_sth(sth);
   CODE:
	{
   	XSRETURN_IV( imp_sth->stmt_type);
}

void
ora_stmt_type_name(sth)
	SV *	sth
	PREINIT:
	D_imp_sth(sth);
	CODE:
	char *p = oci_stmt_type_name(imp_sth->stmt_type);
	SV *sv = sv_newmortal();
	if (p)
	  sv_setpv(sv, p);
	ST(0) = sv;

void
ora_scroll_position(sth)
	SV *	sth
	PREINIT:
	D_imp_sth(sth);
   CODE:
	{
   	XSRETURN_IV( imp_sth->fetch_position);
}

void
ora_fetch_scroll(sth,fetch_orient,fetch_offset)
	SV *	sth
	IV  fetch_orient
	IV 	fetch_offset
	PREINIT:
	D_imp_sth(sth);
	CODE:
	{
	AV *av;
 	imp_sth->fetch_orient=fetch_orient;
	imp_sth->fetch_offset=fetch_offset;
	av = dbd_st_fetch(sth,imp_sth);
    imp_sth->fetch_offset = 1;                  /* default back to 1 for fetch */
 	imp_sth->fetch_orient=OCI_FETCH_NEXT;       /* default back to fetch next */
	ST(0) = (av) ? sv_2mortal(newRV((SV *)av)) : &PL_sv_undef;
}

void
ora_bind_param_inout_array(sth, param, av_ref, maxlen, attribs)
	SV *	sth
	SV *	param
	SV *	av_ref
	IV 		maxlen
	SV *	attribs
	CODE:
	{
	IV sql_type = 0;
	D_imp_sth(sth);
	SV *av_value;
	if (!SvROK(av_ref) || SvTYPE(SvRV(av_ref)) != SVt_PVAV)
	 	 croak("bind_param_inout_array needs a reference to a array value");
	av_value = av_ref;
	if (SvREADONLY(av_value))
		croak("Modification of a read-only value attempted");
	if (attribs) {
		if (SvNIOK(attribs)) {
			sql_type = SvIV(attribs);
			attribs = Nullsv;
		}
		else {
	   	 	SV **svp;
			DBD_ATTRIBS_CHECK("bind_param", sth, attribs);
			DBD_ATTRIB_GET_IV(attribs, "ora_type",4, svp, sql_type);
		}
	}
	ST(0) = dbd_bind_ph(sth, imp_sth, param,av_value, sql_type, attribs, TRUE, maxlen)
		? &PL_sv_yes : &PL_sv_no;
}


void
ora_fetch(sth)
	SV *	sth
	PPCODE:
	/* fetchrow: but with scalar fetch returning NUM_FIELDS for Oraperl	*/
	/* This code is called _directly_ by Oraperl.pm bypassing the DBI.	*/
	/* as a result we have to do some things ourselves (like calling	*/
	/* CLEAR_ERROR) and we loose the tracing that the DBI offers :-(	*/
	D_imp_sth(sth);
	AV *av;
	int debug = DBIc_DEBUGIV(imp_sth);
	if (DBIS->debug > debug)
	debug = DBIS->debug;
	DBIh_CLEAR_ERROR(imp_sth);
	if (GIMME == G_SCALAR) {	/* XXX Oraperl	*/
	/* This non-standard behaviour added only to increase the	*/
	/* performance of the oraperl emulation layer (Oraperl.pm)	*/
	if (!imp_sth->done_desc && !dbd_describe(sth, imp_sth))
		XSRETURN_UNDEF;
	XSRETURN_IV(DBIc_NUM_FIELDS(imp_sth));
	}
	if (debug >= 2)
		PerlIO_printf(DBILOGFP, "	-> ora_fetch\n");
	av = dbd_st_fetch(sth, imp_sth);
	if (av) {
	int num_fields = AvFILL(av)+1;
	int i;
	EXTEND(sp, num_fields);
	for(i=0; i < num_fields; ++i) {
		PUSHs(AvARRAY(av)[i]);
	}
	if (debug >= 2)
		PerlIO_printf(DBILOGFP, "	<- (...) [%d items]\n", num_fields);
	}
	else {
	if (debug >= 2)
		PerlIO_printf(DBILOGFP, "	<- () [0 items]\n");
	}
	if (debug >= 2 && SvTRUE(DBIc_ERR(imp_sth)))
		PerlIO_printf(DBILOGFP, "	!! ERROR: %s %s",
			neatsvpv(DBIc_ERR(imp_sth),0), neatsvpv(DBIc_ERRSTR(imp_sth),0));

void
ora_execute_array(sth, tuples, exe_count, tuples_status, err_count, cols=&PL_sv_undef)
	SV *	sth
	SV *	tuples
	IV		exe_count
	SV *	tuples_status
	SV *	cols
	SV *	err_count
	PREINIT:
	D_imp_sth(sth);
	int retval;
	CODE:
	/* XXX Need default bindings if any phs are so far unbound(?) */
	/* XXX this code is duplicated in selectrow_arrayref above  */
	if (DBIc_ROW_COUNT(imp_sth) > 0) /* reset for re-execute */
		DBIc_ROW_COUNT(imp_sth) = 0;
	retval = ora_st_execute_array(sth, imp_sth, tuples, tuples_status,
								  cols, (ub4)exe_count,err_count);
	/* XXX Handle return value ... like DBI::execute_array(). */
	/* remember that dbd_st_execute must return <= -2 for error */
	if (retval == 0)			/* ok with no rows affected	 */
		XST_mPV(0, "0E0");	  /* (true but zero)			  */
	else if (retval < -1)	   /* -1 == unknown number of rows */
		XST_mUNDEF(0);		  /* <= -2 means error			*/
	else
		XST_mIV(0, retval);	 /* typically 1, rowcount or -1  */


void
cancel(sth)
	SV *		sth
	CODE:
	D_imp_sth(sth);
	ST(0) = dbd_st_cancel(sth, imp_sth) ? &PL_sv_yes : &PL_sv_no;



# ------------------------------------------------------------
# database level interface
# ------------------------------------------------------------
MODULE = DBD::Oracle	PACKAGE = DBD::Oracle::db

void
ora_db_startup(dbh, attribs)
	SV *dbh
	SV *attribs
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
#if defined(ORA_OCI_102)
	ub4 mode;
	ub4 flags;
	OCIAdmin *admhp;
	STRLEN svp_len;
	text *str;
#endif
	CODE:
#if defined(ORA_OCI_102)
	SV **svp;
	mode = OCI_DEFAULT;
	DBD_ATTRIB_GET_IV(attribs, "ora_mode", 8, svp, mode);
	flags = OCI_DEFAULT;
	DBD_ATTRIB_GET_IV(attribs, "ora_flags", 9, svp, flags);
    admhp = (OCIAdmin*)0;
	if ((svp=DBD_ATTRIB_GET_SVP(attribs, "ora_pfile", 9)) && SvOK(*svp)) {
		if (!SvPOK(*svp))
			croak("ora_pfile is not a string");
		str = (text*)SvPV(*svp, svp_len);
		OCIHandleAlloc(imp_dbh->envhp, (dvoid**)&admhp, (ub4)OCI_HTYPE_ADMIN, (size_t)0, (dvoid**)0);
		OCIAttrSet_log_stat(imp_dbh, (dvoid*)admhp, (ub4)OCI_HTYPE_ADMIN, (dvoid*)str, (ub4)svp_len, (ub4)OCI_ATTR_ADMIN_PFILE, (OCIError*)imp_dbh->errhp, status);
  }
	OCIDBStartup_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, admhp, mode, flags, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCIDBStartup");
		ST(0) = &PL_sv_undef;
	}
	else {
		ST(0) = &PL_sv_yes;
	}
	if (admhp) OCIHandleFree_log_stat(imp_dbh, (dvoid*)admhp, (ub4)OCI_HTYPE_ADMIN, status);
#else
	croak("OCIDBStartup not available");
#endif


void
ora_db_shutdown(dbh, attribs)
	SV *dbh
	SV *attribs
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
#if defined(ORA_OCI_102)
	ub4 mode;
	OCIAdmin *admhp;
#endif
	CODE:
#if defined(ORA_OCI_102)
	SV **svp;
	mode = OCI_DEFAULT;
	DBD_ATTRIB_GET_IV(attribs, "ora_mode", 8, svp, mode);
	admhp = (OCIAdmin*)0;
	OCIDBShutdown_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, admhp, mode, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCIDBShutdown");
		ST(0) = &PL_sv_undef;
	}
	else {
		ST(0) = &PL_sv_yes;
	}
#else
	croak("OCIDBShutdown not available");
#endif

void
ora_can_taf(dbh)
	SV 				*dbh
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
	ub4 can_taf = 0;
	CODE:
#ifdef OCI_ATTR_TAF_ENABLED
	OCIAttrGet_log_stat(imp_dbh, imp_dbh->srvhp, OCI_HTYPE_SERVER, &can_taf, NULL,
				OCI_ATTR_TAF_ENABLED, imp_dbh->errhp, status);
	if (status != OCI_SUCCESS) {
# else
    if ( 1 ) {
# endif
		oci_error(dbh, imp_dbh->errhp, status, "OCIAttrGet OCI_ATTR_TAF_ENABLED");
		XSRETURN_IV(0);
	}
	else {
		XSRETURN_IV(can_taf);
	}

void
ora_ping(dbh)
	SV *dbh
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
#if defined(ORA_OCI_102)
	ub4 vernum;
#endif
 	text buf[2];
	CODE:
	/*when OCIPing not available,*/
	/*simply does a call to OCIServerVersion which should make 1 round trip*/
	/*later I will replace this with the actual OCIPing command*/
	/*This will work if the DB goes down, */
	/*If the listener goes down it is another case as the Listener is needed to establish the connection not maintain it*/
	/*so we should stay connected but we cannot get nay new connections*/
	{
        /* RT 69059 - despite OCIPing being introduced in 10.2
         * it is not available in all versions of 10.2 for AIX
         * e.g., 10.2.0.4 does not have it and 10.2.0.5 does
         * see http://comments.gmane.org/gmane.comp.lang.perl.modules.dbi.general/16206
         * We don't do versions to that accuracy so for AIX you have
         * to wait until 11.2 for OCIPing.
         *
         * Further comments on dbi-dev
         * "DBD::Oracle RTs a summary and request for help" suggested it
         * was Oracle bug 5759845 and fixes in 10.2.0.2.
         */
#if !defined(ORA_OCI_102) || (defined(_AIX) && !defined(ORA_OCI_112))
	OCIServerVersion_log_stat(imp_dbh, imp_dbh->svchp,imp_dbh->errhp,buf,2,OCI_HTYPE_SVCCTX,status);
#else
	vernum = ora_db_version(dbh,imp_dbh);
	/* OCIPing causes server failures if called against server ver < 10.2 */
	if (((int)((vernum>>24) & 0xFF) < 10 ) || (((int)((vernum>>24) & 0xFF) == 10 ) && ((int)((vernum>>20) & 0x0F) < 2 ))){
		OCIServerVersion_log_stat(imp_dbh, imp_dbh->svchp,imp_dbh->errhp,buf,2,OCI_HTYPE_SVCCTX,status);
	} else {
    	OCIPing_log_stat(imp_dbh, imp_dbh->svchp,imp_dbh->errhp,status);
	}
#endif
	if (status != OCI_SUCCESS){
		XSRETURN_IV(0);
	} else {
		XSRETURN_IV(1);
	}
}


void
reauthenticate(dbh, uid, pwd)
	SV *	dbh
	char *	uid
	char *	pwd
	CODE:
	D_imp_dbh(dbh);
	ST(0) = ora_db_reauthenticate(dbh, imp_dbh, uid, pwd) ? &PL_sv_yes : &PL_sv_no;

void
ora_lob_write(dbh, locator, offset, data)
	SV *dbh
	OCILobLocator   *locator
	UV	offset
	SV	*data
	PREINIT:
	D_imp_dbh(dbh);
	ub4 amtp;
	STRLEN data_len; /* bytes not chars */
	dvoid *bufp;
	sword status;
	ub2 csid;
	ub1 csform;
	CODE:
	csid = 0;
	csform = SQLCS_IMPLICIT;
	bufp = SvPV(data, data_len);
	amtp = data_len;
	/* if locator is CLOB and data is UTF8 and not in bytes pragma */
	/* if (0 && SvUTF8(data) && !IN_BYTES) { amtp = sv_len_utf8(data); }  */
	/* added by lab: */
	/* LAB do something about length here? see above comment */
	 OCILobCharSetForm_log_stat(imp_dbh, imp_dbh->envhp, imp_dbh->errhp, locator, &csform, status );
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobCharSetForm");
	ST(0) = &PL_sv_undef;
		return;
	}
#ifdef OCI_ATTR_CHARSET_ID
	/* Effectively only used so AL32UTF8 works properly */
	OCILobCharSetId_log_stat(imp_dbh,
                             imp_dbh->envhp,
                             imp_dbh->errhp,
                             locator,
                             &csid,
                             status );
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobCharSetId");
	ST(0) = &PL_sv_undef;
		return;
	}
#endif /* OCI_ATTR_CHARSET_ID */
	/* if data is utf8 but charset isn't then switch to utf8 csid */
	csid = (SvUTF8(data) && !CS_IS_UTF8(csid)) ? utf8_csid : CSFORM_IMPLIED_CSID(csform);

	OCILobWrite_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator,
		&amtp, (ub4)offset,
		bufp, (ub4)data_len, OCI_ONE_PIECE,
		NULL, NULL,
		(ub2)0, csform , status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobWrite");
	ST(0) = &PL_sv_undef;
	}
	else {
		ST(0) = &PL_sv_yes;
	}

void
ora_lob_append(dbh, locator, data)
	SV *dbh
	OCILobLocator   *locator
	SV	*data
	PREINIT:
	D_imp_dbh(dbh);
	ub4 amtp;
	STRLEN data_len; /* bytes not chars */
	dvoid *bufp;
	sword status;
#if !defined(OCI_HTYPE_DIRPATH_FN_CTX) /* Oracle is < 9.0 */
	ub4 startp;
#endif
	ub1 csform;
	ub2 csid;
	CODE:
	csid = 0;
	csform = SQLCS_IMPLICIT;
	bufp = SvPV(data, data_len);
	amtp = data_len;
	/* if locator is CLOB and data is UTF8 and not in bytes pragma */
	/* if (1 && SvUTF8(data) && !IN_BYTES) */
	/* added by lab: */
	/* LAB do something about length here? see above comment */
	OCILobCharSetForm_log_stat(imp_dbh, imp_dbh->envhp, imp_dbh->errhp, locator, &csform, status );
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobCharSetForm");
	ST(0) = &PL_sv_undef;
		return;
	}
#ifdef OCI_ATTR_CHARSET_ID
	/* Effectively only used so AL32UTF8 works properly */
	OCILobCharSetId_log_stat(imp_dbh,
                             imp_dbh->envhp,
                             imp_dbh->errhp,
                             locator,
                             &csid,
                             status );
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobCharSetId");
	ST(0) = &PL_sv_undef;
		return;
	}
#endif /* OCI_ATTR_CHARSET_ID */
	/* if data is utf8 but charset isn't then switch to utf8 csid */
	csid = (SvUTF8(data) && !CS_IS_UTF8(csid)) ? utf8_csid : CSFORM_IMPLIED_CSID(csform);
	OCILobWriteAppend_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator,
				   &amtp, bufp, (ub4)data_len, OCI_ONE_PIECE,
				   NULL, NULL,
				   csid, csform, status);
	if (status != OCI_SUCCESS) {
	   oci_error(dbh, imp_dbh->errhp, status, "OCILobWriteAppend");
	   ST(0) = &PL_sv_undef;
	}
	else {
	   ST(0) = &PL_sv_yes;
	}




void
ora_lob_read(dbh, locator, offset, length)
	SV *dbh
	OCILobLocator   *locator
	UV	offset
	UV	length
	PREINIT:
	D_imp_dbh(dbh);
	ub4 amtp;
	STRLEN bufp_len;
	SV *dest_sv;
	dvoid *bufp;
	sword status;
	ub1 csform;
	CODE:

	csform = SQLCS_IMPLICIT;
    /* NOTE, if length is 0 this will create an empty SV of undef
       see RT55028 */
	dest_sv = sv_2mortal(newSV(length*4)); /*LAB: crude hack that works... tim did it else where XXX */

    if (length > 0) {
        SvPOK_on(dest_sv);
        bufp_len = SvLEN(dest_sv);	/* XXX bytes not chars? (lab: yes) */
        bufp = SvPVX(dest_sv);
        amtp = length;	/* if utf8 and clob/nclob: in: chars, out: bytes */
        /* http://www.lc.leidenuniv.nl/awcourse/oracle/appdev.920/a96584/oci16m40.htm#427818 */
        /* if locator is CLOB and data is UTF8 and not in bytes pragma */
        /* if (0 && SvUTF8(dest_sv) && !IN_BYTES) { amtp = sv_len_utf8(dest_sv); }  */
        /* added by lab: */
        OCILobCharSetForm_log_stat(imp_dbh,  imp_dbh->envhp, imp_dbh->errhp, locator, &csform, status );
        if (status != OCI_SUCCESS) {
            oci_error(dbh, imp_dbh->errhp, status, "OCILobCharSetForm");
            dest_sv = &PL_sv_undef;
            return;
        }
        {
            /* see rt 75163 */
            boolean is_open;

            OCILobFileIsOpen_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator, &is_open, status);
            if (status == OCI_SUCCESS && !is_open) {
                OCILobFileOpen_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator,
                                        (ub1)OCI_FILE_READONLY, status);
                if (status != OCI_SUCCESS) {
                    oci_error(dbh, imp_dbh->errhp, status, "OCILobFileOpen");
                    dest_sv = &PL_sv_undef;
                }
            }
        }

        OCILobRead_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator,
                            &amtp, (ub4)offset, /* offset starts at 1 */
                            bufp, (ub4)bufp_len,
                            0, 0, (ub2)0, csform, status);
        if (status != OCI_SUCCESS) {
            oci_error(dbh, imp_dbh->errhp, status, "OCILobRead");
            dest_sv = &PL_sv_undef;
        }
        else {
            SvCUR(dest_sv) = amtp; /* always bytes here */
            *SvEND(dest_sv) = '\0';
            if (csform){
                if (CSFORM_IMPLIES_UTF8(csform)){
                    SvUTF8_on(dest_sv);
                }
            }
        }
    } /* length > 0 */

	ST(0) = dest_sv;

void
ora_lob_trim(dbh, locator, length)
	SV *dbh
	OCILobLocator   *locator
	UV	length
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
	CODE:
	OCILobTrim_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator, length, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobTrim");
	ST(0) = &PL_sv_undef;
	}
	else {
	ST(0) = &PL_sv_yes;
	}

void
ora_lob_is_init(dbh, locator)
	SV *dbh
	OCILobLocator   *locator
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
	boolean is_init = 0;
	CODE:
	OCILobLocatorIsInit_log_stat(imp_dbh, imp_dbh->envhp,imp_dbh->errhp,locator,&is_init,status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobLocatorIsInit ora_lob_is_init");
	    ST(0) = &PL_sv_undef;
	}
	else {
	    ST(0) = sv_2mortal(newSVuv(is_init));
	}

void
ora_lob_length(dbh, locator)
	SV 				*dbh
	OCILobLocator   *locator
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
	ub4 len = 0;
	CODE:
	OCILobGetLength_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator, &len, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobGetLength ora_lob_length");
	ST(0) = &PL_sv_undef;
	}
	else {
	ST(0) = sv_2mortal(newSVuv(len));
	}


void
ora_lob_chunk_size(dbh, locator)
	SV *dbh
	OCILobLocator   *locator
	PREINIT:
	D_imp_dbh(dbh);
	sword status;
	ub4 chunk_size = 0;
	CODE:
	OCILobGetChunkSize_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, locator, &chunk_size, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCILobGetChunkSize");
		ST(0) = &PL_sv_undef;
	}
	else {
		ST(0) = sv_2mortal(newSVuv(chunk_size));
	}



# ------------------------------------------------------------
# driver level interface
# ------------------------------------------------------------
MODULE = DBD::Oracle	PACKAGE = DBD::Oracle::dr

void
init_oci(drh)
	SV *	drh
	CODE:
	D_imp_drh(drh);
	dbd_init_oci(DBIS) ;
	dbd_init_oci_drh(imp_drh) ;

void
DESTROY(drh)
    SV *        drh
    PPCODE:
    /* keep in sync with default DESTROY in DBI.xs (currently there is no dr default) */
    D_imp_drh(drh);
    ST(0) = &PL_sv_yes;
    dbd_dr_destroy(drh, imp_drh);
