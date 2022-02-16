/*
	vim:sw=4:ts=8
	oci8.c

	Copyright (c) 1998-2006  Tim Bunce  Ireland
	Copyright (c) 2006-2008 John Scoles (The Pythian Group), Canada

	See the COPYRIGHT section in the Oracle.pm file for terms.

*/

#include "Oracle.h"

#ifdef UTF8_SUPPORT
#include <utf8.h>
#endif

#undef sv_set_undef
#define sv_set_undef(sv) if (SvROK(sv)) sv_unref(sv); else SvOK_off(sv)

DBISTATE_DECLARE;

int describe_obj_by_tdo(SV *sth,imp_sth_t *imp_sth,fbh_obj_t *obj,ub2 level );
int dump_struct(imp_sth_t *imp_sth,fbh_obj_t *obj,int level);


/*
char *
dbd_yes_no(int yes_no)
{
	dTHX;
	if (yes_no) {
		return "Yes";
	}
	return "No";
}
*/

void
dbd_init_oci(dbistate_t *dbistate)
{
	dTHX;
	DBIS = dbistate;
}

void
dbd_init_oci_drh(imp_drh_t * imp_drh)
{
	dTHX;
	imp_drh->ora_long	= perl_get_sv("Oraperl::ora_long",	  GV_ADDMULTI);
	imp_drh->ora_trunc	= perl_get_sv("Oraperl::ora_trunc",	 GV_ADDMULTI);
	imp_drh->ora_cache	= perl_get_sv("Oraperl::ora_cache",	 GV_ADDMULTI);
	imp_drh->ora_cache_o = perl_get_sv("Oraperl::ora_cache_o",	GV_ADDMULTI);

}

/*
char *
oci_sql_function_code_name(int sqlfncode)
{
	dTHX;
	SV *sv;
	switch (sqlfncode) {
		case 1 :	return "CREATE TABLE";
		case 3 :	return "INSERT";
		case 4 :	return "SELECT";
		case 5 :	return "UPDATE";
		case 8 :	return "DROP TABLE";
		case 9 :	return "DELETE";

	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN SQL FN Code %d)", sqlfncode);
	return SvPVX(sv);
}
*/

 /*
char *
oci_ptype_name(int ptype)
{
	dTHX;
	SV *sv;
	switch (ptype) {
		case OCI_PTYPE_UNK:			return "UNKNOWN";
		case OCI_PTYPE_TABLE:		return "TABLE";
		case OCI_PTYPE_VIEW:		return "VIEW";
		case OCI_PTYPE_PROC:		return "PROCEDURE";
		case OCI_PTYPE_FUNC:		return "FUNCTION";
		case OCI_PTYPE_PKG:			return "PACKAGE";
		case OCI_PTYPE_TYPE:		return "USER DEFINED TYPE";
		case OCI_PTYPE_SYN:			return "SYNONYM";
		case OCI_PTYPE_SEQ:			return "SEQUENCE";
		case OCI_PTYPE_COL:			return "COLUMN";
		case OCI_PTYPE_ARG:			return "ARGUMENT";
		case OCI_PTYPE_LIST:		return "LIST";
		case OCI_PTYPE_TYPE_ATTR:	return "USER-DEFINED TYPE'S ATTRIBUTE";
		case OCI_PTYPE_TYPE_COLL:	return "COLLECTION TYPE'S ELEMENT";
		case OCI_PTYPE_TYPE_METHOD:	return "USER-DEFINED TYPE'S METHOD";
		case OCI_PTYPE_TYPE_ARG:	return "USER-DEFINED TYPE METHOD'S ARGUMENT";
		case OCI_PTYPE_TYPE_RESULT:	return "USER-DEFINED TYPE METHOD'S RESULT";
		case OCI_PTYPE_SCHEMA:		return "SCHEMA";
		case OCI_PTYPE_DATABASE:		return "DATABASE";

	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN PTYPE Code %d)", ptype);
	return SvPVX(sv);
}
 */

char *
oci_exe_mode(ub4 mode)
{

	dTHX;
	SV *sv;
	switch (mode) {
	/*----------------------- Execution Modes -----------------------------------*/
		case OCI_DEFAULT:			return "DEFAULT";
		case OCI_BATCH_MODE:		return "BATCH_MODE"; /* batch the oci stmt for exec */
		case OCI_EXACT_FETCH:		return "EXACT_FETCH";	/* fetch exact rows specified */
		case OCI_STMT_SCROLLABLE_READONLY :		return "STMT_SCROLLABLE_READONLY";
		case OCI_DESCRIBE_ONLY:		return "DESCRIBE_ONLY";  /* only describe the statement */
		case OCI_COMMIT_ON_SUCCESS:	return "COMMIT_ON_SUCCESS";	/* commit, if successful exec */
		case OCI_NON_BLOCKING:		return "NON_BLOCKING";				/* non-blocking */
		case OCI_BATCH_ERRORS:		return "BATCH_ERRORS";	/* batch errors in array dmls */
		case OCI_PARSE_ONLY:		return "PARSE_ONLY";	 /* only parse the statement */
		case OCI_SHOW_DML_WARNINGS:	return "SHOW_DML_WARNINGS";
/*		case OCI_RESULT_CACHE:		return "RESULT_CACHE";	hint to use query caching only 11 so wait this one out*/
/*		case OCI_NO_RESULT_CACHE :	return "NO_RESULT_CACHE";	hint to bypass query caching*/
	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN OCI EXECUTE MODE %d)", mode);
	return SvPVX(sv);
}

/* SQL Types we support for placeholders basically we support types that can be returned as strings */
char *
sql_typecode_name(int dbtype) {
	dTHX;
	SV *sv;
	switch(dbtype) {
		case  0:	return "DEFAULT (varchar)";
		case  1:	return "VARCHAR";
		case  2:	return "NVARCHAR2";
		case  5:	return "STRING";
		case  8:	return "LONG";
		case 21:	return "BINARY FLOAT os-endian";
		case 22:	return "BINARY DOUBLE os-endian";
		case 23:	return "RAW";
		case 24:	return "LONG RAW";
		case 96:	return "CHAR";
		case 97:	return "CHARZ";
		case 100:	return "BINARY FLOAT oracle-endian";
		case 101:	return "BINARY DOUBLE oracle-endian";
                case 104:       return "ROWID";
		case 106:	return "MLSLABEL";
		case 102:	return "SQLT_CUR	OCI 7 cursor variable";
		case 112:	return "SQLT_CLOB / long";
		case 113:	return "SQLT_BLOB / long";
		case 116:	return "SQLT_RSET	OCI 8 cursor variable";
		case ORA_VARCHAR2_TABLE:return "ORA_VARCHAR2_TABLE";
		case ORA_NUMBER_TABLE: 	return "ORA_NUMBER_TABLE";
		case ORA_XMLTYPE:		return "ORA_XMLTYPE or SQLT_NTY";/* SQLT_NTY	must be careful here as its value (108) is the same for an embedded object Well really only XML clobs not embedded objects  */

	}
	 sv = sv_2mortal(newSVpv("",0));
	 sv_grow(sv, 50);
	 sprintf(SvPVX(sv),"(UNKNOWN SQL TYPECODE %d)", dbtype);
	 return SvPVX(sv);
}



char *
oci_typecode_name(int typecode){

	dTHX;
	SV *sv;

	switch (typecode) {
		case OCI_TYPECODE_INTERVAL_YM:		return "INTERVAL_YM";
		case OCI_TYPECODE_INTERVAL_DS:		return "NTERVAL_DS";
		case OCI_TYPECODE_TIMESTAMP_TZ:		return "TIMESTAMP_TZ";
		case OCI_TYPECODE_TIMESTAMP_LTZ:	return "TIMESTAMP_LTZ";
		case OCI_TYPECODE_TIMESTAMP:		return "TIMESTAMP";
		case OCI_TYPECODE_DATE:				return "DATE";
		case OCI_TYPECODE_CLOB:				return "CLOB";
		case OCI_TYPECODE_BLOB:				return "BLOB";
		case OCI_TYPECODE_BFILE:			return "BFILE";
		case OCI_TYPECODE_RAW:				return "RAW";
		case OCI_TYPECODE_CHAR:				return "CHAR";
		case OCI_TYPECODE_VARCHAR:			return "VARCHAR";
		case OCI_TYPECODE_VARCHAR2:			return "VARCHAR2";
		case OCI_TYPECODE_SIGNED8:			return "SIGNED8";
		case OCI_TYPECODE_UNSIGNED8:		return "DECLARE";
		case OCI_TYPECODE_UNSIGNED16 :		return "UNSIGNED8";
		case OCI_TYPECODE_UNSIGNED32 :		return "UNSIGNED32";
		case OCI_TYPECODE_REAL :			return "REAL";
		case OCI_TYPECODE_DOUBLE :			return "DOUBLE";
		case OCI_TYPECODE_INTEGER :			return "INT";
		case OCI_TYPECODE_SIGNED16 :		return "SHORT";
		case OCI_TYPECODE_SIGNED32 :		return "LONG";
		case OCI_TYPECODE_DECIMAL :			return "DECIMAL";
		case OCI_TYPECODE_FLOAT :			return "FLOAT";
		case OCI_TYPECODE_NUMBER : 			return "NUMBER";
		case OCI_TYPECODE_SMALLINT:			return "SMALLINT";
		case OCI_TYPECODE_OBJECT:			return "OBJECT";
		case OCI_TYPECODE_OPAQUE:			return "XMLTYPE~OPAQUE";
		case OCI_TYPECODE_VARRAY:			return "VARRAY";
		case OCI_TYPECODE_TABLE:			return "TABLE";
		case OCI_TYPECODE_NAMEDCOLLECTION:	return "NAMEDCOLLECTION";
	}

	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN OCI TYPECODE %d)", typecode);
	return SvPVX(sv);

}

char *
oci_status_name(sword status)
{
	dTHX;
	SV *sv;
	switch (status) {
		case OCI_SUCCESS:			return "SUCCESS";
		case OCI_SUCCESS_WITH_INFO:	return "SUCCESS_WITH_INFO";
		case OCI_NEED_DATA:			return "NEED_DATA";
		case OCI_NO_DATA:			return "NO_DATA";
		case OCI_ERROR:				return "ERROR";
		case OCI_INVALID_HANDLE:	return "INVALID_HANDLE";
		case OCI_STILL_EXECUTING:	return "STILL_EXECUTING";
		case OCI_CONTINUE:			return "CONTINUE";
	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN OCI STATUS %d)", status);
	return SvPVX(sv);
}
/* the various modes used in OCI */
char *
oci_define_options(ub4 options)
{
	dTHX;
	SV *sv;
	switch (options) {
	/*------------------------Bind and Define Options----------------------------*/
		case OCI_DEFAULT:		return "DEFAULT";
		case OCI_DYNAMIC_FETCH: return "DYNAMIC_FETCH";				/* fetch dynamically */

	 }
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN OCI DEFINE MODE %d)", options);
	return SvPVX(sv);
}

char *
oci_bind_options(ub4 options)
{
	dTHX;
	SV *sv;
	switch (options) {
	/*------------------------Bind and Define Options----------------------------*/
		case OCI_DEFAULT:		return "DEFAULT";
		case OCI_SB2_IND_PTR:	return "SB2_IND_PTR";						  /* unused */
		case OCI_DATA_AT_EXEC:	return "DATA_AT_EXEC";			 /* data at execute time */
		case OCI_PIECEWISE:		return "PIECEWISE";		 /* piecewise DMLs or fetch */
/*		case OCI_BIND_SOFT:		return "BIND_SOFT";				soft bind or define */
/*		case OCI_DEFINE_SOFT:	return "DEFINE_SOFT";			soft bind or define */
/*		case OCI_IOV:			return "";	11g only release 1.23 me thinks For scatter gather bind/define */

	 }
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN BIND MODE %d)", options);
	return SvPVX(sv);
}

/* the various modes used in OCI */
char *
oci_mode(ub4  mode)
{
	dTHX;
	SV *sv;
	switch (mode) {
		case OCI_THREADED | OCI_OBJECT:	return "THREADED | OBJECT";
		case OCI_OBJECT | OCI_EVENTS:	return "OBJECT | EVENTS";
		case OCI_THREADED | OCI_OBJECT | OCI_EVENTS:	return "THREADED | OBJECT | EVENTS";
		case OCI_DEFAULT:		return "DEFAULT";
		/* the default value for parameters and attributes */
		/*-------------OCIInitialize Modes / OCICreateEnvironment Modes -------------*/
		case OCI_THREADED:		return "THREADED";	  /* appl. in threaded environment */
		case OCI_OBJECT:		return "OBJECT";  /* application in object environment */
		case OCI_EVENTS:		return "EVENTS";  /* application is enabled for events */
		case OCI_SHARED:		return "SHARED";  /* the application is in shared mode */
		/* The following *TWO* are only valid for OCICreateEnvironment call */
		case OCI_NO_UCB:		return "NO_UCB "; /* No user callback called during ini */
		case OCI_NO_MUTEX:		return "NO_MUTEX"; /* the environment handle will not be
											  protected by a mutex internally */
		case OCI_SHARED_EXT:	 return "SHARED_EXT";			  /* Used for shared forms */
		case OCI_ALWAYS_BLOCKING:return "ALWAYS_BLOCKING";	/* all connections always blocking */
		case OCI_USE_LDAP:		return "USE_LDAP";			/* allow  LDAP connections */
		case OCI_REG_LDAPONLY:	return "REG_LDAPONLY";			  /* only register to LDAP */
		case OCI_UTF16:			return "UTF16";		/* mode for all UTF16 metadata */
		case OCI_AFC_PAD_ON:	return "AFC_PAD_ON";  /* turn on AFC blank padding when rlenp present */
		case OCI_NEW_LENGTH_SEMANTICS: return "NEW_LENGTH_SEMANTICS";	/* adopt new length semantics
													the new length semantics, always bytes, is used by OCIEnvNlsCreate */
		case OCI_NO_MUTEX_STMT:	return "NO_MUTEX_STMT";			/* Do not mutex stmt handle */
		case OCI_MUTEX_ENV_ONLY:	return "MUTEX_ENV_ONLY";  /* Mutex only the environment handle */
/*		case OCI_SUPPRESS_NLS_VALIDATION:return "SUPPRESS_NLS_VALIDATION";	suppress nls validation*/
													 /* 	  nls validation suppression is on by default;*/
													  /*	use OCI_ENABLE_NLS_VALIDATION to disable it */
/*		case OCI_MUTEX_TRY:				return "MUTEX_TRY";	 try and acquire mutex */
/*		case OCI_NCHAR_LITERAL_REPLACE_ON: return "NCHAR_LITERAL_REPLACE_ON";  nchar literal replace on */
/*		case OCI_NCHAR_LITERAL_REPLACE_OFF:return "NCHAR_LITERAL_REPLACE_OFF";  nchar literal replace off*/
/*		case OCI_ENABLE_NLS_VALIDATION:	return "ENABLE_NLS_VALIDATION";	 enable nls validation */
		/*------------------------OCIConnectionpoolCreate Modes----------------------*/
		case OCI_CPOOL_REINITIALIZE:	return "CPOOL_REINITIALIZE";
		/*--------------------------------- OCILogon2 Modes -------------------------*/
/*case OCI_LOGON2_SPOOL:		return "LOGON2_SPOOL";	  Use session pool */
		case OCI_LOGON2_CPOOL:		return "LOGON2_CPOOL"; /* Use connection pool */
/*case OCI_LOGON2_STMTCACHE:	return "LOGON2_STMTCACHE";	  Use Stmt Caching */
		case OCI_LOGON2_PROXY:		return "LOGON2_PROXY";	 /* Proxy authentication */
		/*------------------------- OCISessionPoolCreate Modes ----------------------*/
/*case OCI_SPC_REINITIALIZE:		return "SPC_REINITIALIZE";	Reinitialize the session pool */
/*case OCI_SPC_HOMOGENEOUS: 		return "SPC_HOMOGENEOUS"; "";	Session pool is homogeneous */
/*case OCI_SPC_STMTCACHE:			return "SPC_STMTCACHE";	Session pool has stmt cache */
/*case OCI_SPC_NO_RLB:			return "SPC_NO_RLB ";  Do not enable Runtime load balancing. */
		/*--------------------------- OCISessionGet Modes ---------------------------*/
/*case OCI_SESSGET_SPOOL:	 	return "SESSGET_SPOOL";	  SessionGet called in SPOOL mode */
/*case OCI_SESSGET_CPOOL:			return "SESSGET_CPOOL";	SessionGet called in CPOOL mode */
/*case OCI_SESSGET_STMTCACHE: 	return "SESSGET_STMTCACHE";				  Use statement cache */
/*case OCI_SESSGET_CREDPROXY: 	return "SESSGET_CREDPROXY";	  SessionGet called in proxy mode */
/*case OCI_SESSGET_CREDEXT:		return "SESSGET_CREDEXT";	 */
		case OCI_SESSGET_SPOOL_MATCHANY:return "SESSGET_SPOOL_MATCHANY";
/*case OCI_SESSGET_PURITY_NEW:	return "SESSGET_PURITY_NEW";
		case OCI_SESSGET_PURITY_SELF:	return "SESSGET_PURITY_SELF"; */
	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(UNKNOWN OCI MODE %d)", mode);
	return SvPVX(sv);
}

char *
oci_stmt_type_name(int stmt_type)
{
	dTHX;
	SV *sv;
	switch (stmt_type) {
	case OCI_STMT_SELECT:	return "SELECT";
	case OCI_STMT_UPDATE:	return "UPDATE";
	case OCI_STMT_DELETE:	return "DELETE";
	case OCI_STMT_INSERT:	return "INSERT";
	case OCI_STMT_CREATE:	return "CREATE";
	case OCI_STMT_DROP:		return "DROP";
	case OCI_STMT_ALTER:	return "ALTER";
	case OCI_STMT_BEGIN:	return "BEGIN";
	case OCI_STMT_DECLARE:	return "DECLARE";
	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"(STMT TYPE %d)", stmt_type);
	return SvPVX(sv);
}

char *
oci_col_return_codes(int rc)
{
	dTHX;
	SV *sv;
	switch (rc) {
		case 1406:	return "TRUNCATED";
		case 0:		return "OK";
		case 1405:	return "NULL";
		case 1403:	return "NO DATA";

	}
	sv = sv_2mortal(newSVpv("",0));
	sv_grow(sv, 50);
	sprintf(SvPVX(sv),"UNKNOWN RC=%d)", rc);
	return SvPVX(sv);
}

char *
oci_hdtype_name(ub4 hdtype)
{
	dTHX;
	SV *sv;
	switch (hdtype) {
	/* Handles */
	case OCI_HTYPE_ENV:				return "OCI_HTYPE_ENV";
	case OCI_HTYPE_ERROR:			return "OCI_HTYPE_ERROR";
	case OCI_HTYPE_SVCCTX:			return "OCI_HTYPE_SVCCTX";
	case OCI_HTYPE_STMT:			return "OCI_HTYPE_STMT";
	case OCI_HTYPE_BIND:			return "OCI_HTYPE_BIND";
	case OCI_HTYPE_DEFINE:			return "OCI_HTYPE_DEFINE";
	case OCI_HTYPE_DESCRIBE:		return "OCI_HTYPE_DESCRIBE";
	case OCI_HTYPE_SERVER:			return "OCI_HTYPE_SERVER";
	case OCI_HTYPE_SESSION:			return "OCI_HTYPE_SESSION";
	case OCI_HTYPE_CPOOL:   		return "OCI_HTYPE_CPOOL";
	case OCI_HTYPE_SPOOL:   		return "OCI_HTYPE_SPOOL";
	/*case OCI_HTYPE_AUTHINFO:        return "OCI_HTYPE_AUTHINFO";*/
	/* Descriptors */
	case OCI_DTYPE_LOB:				return "OCI_DTYPE_LOB";
	case OCI_DTYPE_SNAP:			return "OCI_DTYPE_SNAP";
	case OCI_DTYPE_RSET:			return "OCI_DTYPE_RSET";
	case OCI_DTYPE_PARAM:			return "OCI_DTYPE_PARAM";
	case OCI_DTYPE_ROWID:			return "OCI_DTYPE_ROWID";
#ifdef OCI_DTYPE_REF
	case OCI_DTYPE_REF:				return "OCI_DTYPE_REF";
#endif
	}
	sv = sv_2mortal(newSViv((IV)hdtype));
	return SvPV(sv,PL_na);
}

/*used to look up the name of a csform value
  used only for debugging */
char *
oci_csform_name(ub4 attr)
{
	dTHX;
	SV *sv;
	switch (attr) {

/* CHAR/NCHAR/VARCHAR2/NVARCHAR2/CLOB/NCLOB char set "form" information */
	case SQLCS_IMPLICIT:			return "SQLCS_IMPLICIT";/* for CHAR, VARCHAR2, CLOB w/o a specified set */
	case SQLCS_NCHAR:				return "SQLCS_NCHAR";/* for NCHAR, NCHAR VARYING, NCLOB */
	case SQLCS_EXPLICIT:			return "SQLCS_EXPLICIT";/* for CHAR, etc, with "CHARACTER SET ..." syntax */
	case SQLCS_FLEXIBLE:			return "SQLCS_FLEXIBLE";/* for PL/SQL "flexible" parameters */
	case SQLCS_LIT_NULL:			return "SQLCS_LIT_NULL";/* for typecheck of NULL and empty_clob() lits */
	}

	sv = sv_2mortal(newSViv((IV)attr));
	return SvPV(sv,PL_na);
}

/*used to look up the name of a OCI_DTYPE_PARAM Attribute Types
  used only for debugging */
char *
oci_dtype_attr_name(ub4 attr)
{
	dTHX;
	SV *sv;
	switch (attr) {
/*=======================Describe Handle Parameter Attributes ===============*/
	case OCI_ATTR_DATA_SIZE:			return "OCI_ATTR_DATA_SIZE";	/* maximum size of the data */
	case OCI_ATTR_DATA_TYPE:			return "OCI_ATTR_DATA_TYPE";	/* the SQL type of the column/argument */
	case OCI_ATTR_DISP_SIZE:			return "OCI_ATTR_DISP_SIZE";	/* the display size */
	case OCI_ATTR_NAME:					return "OCI_ATTR_NAME";			/* the name of the column/argument */
	case OCI_ATTR_PRECISION:			return "OCI_ATTR_PRECISION";	/* precision if number type */
	case OCI_ATTR_SCALE:				return "OCI_ATTR_SCALE"; 		/* scale if number type */
	case OCI_ATTR_IS_NULL:				return "OCI_ATTR_IS_NULL";		/* is it null ? */
	case OCI_ATTR_TYPE_NAME: 			return "OCI_ATTR_TYPE_NAME";
  /* name of the named data type or a package name for package private types */
	case OCI_ATTR_SCHEMA_NAME: 			return "OCI_ATTR_SCHEMA_NAME";	/* the schema name */
	case OCI_ATTR_SUB_NAME: 			return "OCI_ATTR_SUB_NAME";		/* type name if package private type */
	case OCI_ATTR_POSITION:				return "OCI_ATTR_POSITION";
	case OCI_ATTR_CHAR_USED:            return "OCI_ATTR_CHAR_USED";	/* char length semantics */
	case OCI_ATTR_CHAR_SIZE:             return "OCI_ATTR_CHAR_SIZE";	/* char length */
	case OCI_ATTR_CHARSET_ID:			return "OCI_ATTR_CHARSET_ID";	/* Character Set ID */
	case OCI_ATTR_CHARSET_FORM:			return "OCI_ATTR_CHARSET_FORM";	/* Character Set Form */
	}

	sv = sv_2mortal(newSViv((IV)attr));
	return SvPV(sv,PL_na);

}

/*used to look up the name of non a OCI_DTYPE_PARAM Attribute Types
  used only for debugging */
char *
oci_attr_name(ub4 attr)
{
	dTHX;
	SV *sv;
	switch (attr) {
#ifdef ORA_OCI_102
	case OCI_ATTR_MODULE:                    return "OCI_ATTR_MODULE";        /* module for tracing */
	case OCI_ATTR_ACTION:                    return "OCI_ATTR_ACTION";        /* action for tracing */
	case OCI_ATTR_CLIENT_INFO:               return "OCI_ATTR_CLIENT_INFO";               /* client info */
	case OCI_ATTR_COLLECT_CALL_TIME:         return "OCI_ATTR_COLLECT_CALL_TIME";         /* collect call time */
	case OCI_ATTR_CALL_TIME:                 return "OCI_ATTR_CALL_TIME";         /* extract call time */
	case OCI_ATTR_ECONTEXT_ID:               return "OCI_ATTR_ECONTEXT_ID";      /* execution-id context */
	case OCI_ATTR_ECONTEXT_SEQ:              return "OCI_ATTR_ECONTEXT_SEQ";  /*execution-id sequence num */


	/*------------------------------ Session attributes -------------------------*/
	case OCI_ATTR_SESSION_STATE:             return "OCI_ATTR_SESSION_STATE";             /* session state */

	case OCI_ATTR_SESSION_STATETYPE:         return "OCI_ATTR_SESSION_STATETYPE";        /* session state type */
	case OCI_SESSION_STATELESS_DEF: 		 return "OCI_SESSION_STATELESS_DEF";                    /* valid state types */

	case OCI_ATTR_SESSION_STATE_CLEARED:     return "OCI_ATTR_SESSION_STATE_CLEARED";     /* session state cleared*/
	case OCI_ATTR_SESSION_MIGRATED:          return "OCI_ATTR_SESSION_MIGRATED";       /* did session migrate*/
	case OCI_ATTR_SESSION_PRESERVE_STATE:    return "OCI_ATTR_SESSION_PRESERVE_STATE";    /* preserve session state */
#endif
#ifdef ORA_OCI_112
	case OCI_ATTR_DRIVER_NAME:               return "OCI_ATTR_DRIVER_NAME";               /* Driver Name */
#endif
	case OCI_ATTR_CLIENT_IDENTIFIER:         return "OCI_ATTR_CLIENT_IDENTIFIER";   /* value of client id to set*/

	/*=============================Attribute Types===============================*/
#ifdef ORA_OCI_112
    case OCI_ATTR_PURITY:				return "OCI_ATTR_PURITY"; /* for DRCP session purity */
    case OCI_ATTR_CONNECTION_CLASS:		return "OCI_ATTR_CONNECTION_CLASS"; /* for DRCP connection class */
#endif
	case OCI_ATTR_FNCODE:				return "OCI_ATTR_FNCODE";		/* the OCI function code */
	case OCI_ATTR_OBJECT:				return "OCI_ATTR_OBJECT"; /* is the environment initialized in object mode */
	case OCI_ATTR_NONBLOCKING_MODE:		return "OCI_ATTR_NONBLOCKING_MODE";		/* non blocking mode */
	case OCI_ATTR_SQLCODE:				return "OCI_ATTR_SQLCODE";				/* the SQL verb */
	case OCI_ATTR_ENV:					return "OCI_ATTR_ENV";				/* the environment handle */
	case OCI_ATTR_SERVER:				return "OCI_ATTR_SERVER";			/* the server handle*/
	case OCI_ATTR_SESSION:				return "OCI_ATTR_SESSION";			/* the user session handle*/
	case OCI_ATTR_TRANS:				return "OCI_ATTR_TRANS";			/* the transaction handle */
	case OCI_ATTR_ROW_COUNT:			return "OCI_ATTR_ROW_COUNT";		/* the rows processed so far */
	case OCI_ATTR_SQLFNCODE:			return "OCI_ATTR_SQLFNCODE";		/* the SQL verb of the statement */
	case OCI_ATTR_PREFETCH_ROWS:		return "OCI_ATTR_PREFETCH_ROWS";	/* sets the number of rows to prefetch */
	case OCI_ATTR_NESTED_PREFETCH_ROWS:	return "OCI_ATTR_NESTED_PREFETCH_ROWS"; /* the prefetch rows of nested table*/
	case OCI_ATTR_PREFETCH_MEMORY:		return "OCI_ATTR_PREFETCH_MEMORY";		/* memory limit for rows fetched */
	case OCI_ATTR_NESTED_PREFETCH_MEMORY:return "OCI_ATTR_NESTED_PREFETCH_MEMORY";	/* memory limit for nested rows */
	case OCI_ATTR_CHAR_COUNT:			return "OCI_ATTR_CHAR_COUNT";			 /* this specifies the bind and define size in characters */
	case OCI_ATTR_PDSCL:				return "OCI_ATTR_PDSCL";			/* packed decimal scale*/
	/*case OCI_ATTR_FSPRECISION OCI_ATTR_PDSCL:return "";					 fs prec for datetime data types */
	case OCI_ATTR_PDPRC:				return "OCI_ATTR_PDPRC";			/* packed decimal format*/
	/*case OCI_ATTR_LFPRECISION OCI_ATTR_PDPRC: return "";					fs prec for datetime data types */
	case OCI_ATTR_PARAM_COUNT:			return "OCI_ATTR_PARAM_COUNT";		/* number of column in the select list */
	case OCI_ATTR_ROWID:				return "OCI_ATTR_ROWID";			/* the rowid */
	case OCI_ATTR_CHARSET:				return "OCI_ATTR_CHARSET";			/* the character set value */
	case OCI_ATTR_NCHAR:				return "OCI_ATTR_NCHAR";			/* NCHAR type */
	case OCI_ATTR_USERNAME:				return "OCI_ATTR_USERNAME";			/* username attribute */
	case OCI_ATTR_PASSWORD:				return "OCI_ATTR_PASSWORD";			/* password attribute */
	case OCI_ATTR_STMT_TYPE:			return "OCI_ATTR_STMT_TYPE";		/* statement type */
	case OCI_ATTR_INTERNAL_NAME:		return "OCI_ATTR_INTERNAL_NAME";	/* user friendly global name */
	case OCI_ATTR_EXTERNAL_NAME:		return "OCI_ATTR_EXTERNAL_NAME";	/* the internal name for global txn */
	case OCI_ATTR_XID:					return "OCI_ATTR_XID";				/* XOPEN defined global transaction id */
	case OCI_ATTR_TRANS_LOCK:			return "OCI_ATTR_TRANS_LOCK";		/* */
	case OCI_ATTR_TRANS_NAME:			return "OCI_ATTR_TRANS_NAME";		/* string to identify a global transaction */
	case OCI_ATTR_HEAPALLOC:			return "OCI_ATTR_HEAPALLOC";		/* memory allocated on the heap */
	case OCI_ATTR_CHARSET_FORM:			return "OCI_ATTR_CHARSET_FORM";		/* Character Set Form */
	case OCI_ATTR_MAXDATA_SIZE:			return "OCI_ATTR_MAXDATA_SIZE";		/* Maximum size of data on the server  */
	case OCI_ATTR_CACHE_OPT_SIZE:		return "OCI_ATTR_CACHE_OPT_SIZE";	/* object cache optimal size */
	case OCI_ATTR_CACHE_MAX_SIZE:		return "OCI_ATTR_CACHE_MAX_SIZE";	/* object cache maximum size percentage */
	case OCI_ATTR_PINOPTION:			return "OCI_ATTR_PINOPTION";		/* object cache default pin option */
	case OCI_ATTR_ALLOC_DURATION:		return "OCI_ATTR_ALLOC_DURATION";	/* object cache default allocation duration */
	case OCI_ATTR_PIN_DURATION:			return "OCI_ATTR_PIN_DURATION";		/* object cache default pin duration */
	case OCI_ATTR_FDO:					return "OCI_ATTR_FDO";		/* Format Descriptor object attribute */
	case OCI_ATTR_POSTPROCESSING_CALLBACK:		return "OCI_ATTR_POSTPROCESSING_CALLBACK"; /* Callback to process outbind data */
	case OCI_ATTR_POSTPROCESSING_CONTEXT:		return "OCI_ATTR_POSTPROCESSING_CONTEXT";  /* Callback context to process outbind data */
	case OCI_ATTR_ROWS_RETURNED:		return "OCI_ATTR_ROWS_RETURNED"; 	/* Number of rows returned in current iter - for Bind handles */
	case OCI_ATTR_FOCBK:				return "OCI_ATTR_FOCBK";			/* Failover Callback attribute */
	case OCI_ATTR_IN_V8_MODE:			return "OCI_ATTR_IN_V8_MODE";		/* is the server/service context in V8 mode */
	case OCI_ATTR_LOBEMPTY:				return "OCI_ATTR_LOBEMPTY";			/* empty lob ? */
	case OCI_ATTR_SESSLANG:				return "OCI_ATTR_SESSLANG";			/* session language handle */
	case OCI_ATTR_VISIBILITY:			return "OCI_ATTR_VISIBILITY";		/* visibility */
	case OCI_ATTR_RELATIVE_MSGID:		return "OCI_ATTR_RELATIVE_MSGID";	/* relative message id */
	case OCI_ATTR_SEQUENCE_DEVIATION:	return "OCI_ATTR_SEQUENCE_DEVIATION";	/* sequence deviation */

	case OCI_ATTR_CONSUMER_NAME:		return "OCI_ATTR_CONSUMER_NAME";	/* consumer name */
	case OCI_ATTR_DEQ_MODE:				return "OCI_ATTR_DEQ_MODE";			/* dequeue mode */
	case OCI_ATTR_NAVIGATION:			return "OCI_ATTR_NAVIGATION";		/* navigation */
	case OCI_ATTR_WAIT:					return "OCI_ATTR_WAIT";				/* wait */
	case OCI_ATTR_DEQ_MSGID:			return "OCI_ATTR_DEQ_MSGID";		/* dequeue message id */

	case OCI_ATTR_PRIORITY:				return "OCI_ATTR_PRIORITY";			/* priority */
	case OCI_ATTR_DELAY:				return "OCI_ATTR_DELAY";			/* delay */
	case OCI_ATTR_EXPIRATION:			return "OCI_ATTR_EXPIRATION";		/* expiration */
	case OCI_ATTR_CORRELATION:			return "OCI_ATTR_CORRELATION";		/* correlation id */
	case OCI_ATTR_ATTEMPTS:				return "OCI_ATTR_ATTEMPTS";			/* # of attempts */
	case OCI_ATTR_RECIPIENT_LIST:		return "OCI_ATTR_RECIPIENT_LIST";	/* recipient list */
	case OCI_ATTR_EXCEPTION_QUEUE:		return "OCI_ATTR_EXCEPTION_QUEUE";	/* exception queue name */
	case OCI_ATTR_ENQ_TIME:				return "OCI_ATTR_ENQ_TIME";			/* enqueue time (only OCIAttrGet) */
	case OCI_ATTR_MSG_STATE:			return "OCI_ATTR_MSG_STATE";		/* message state (only OCIAttrGet) */
																			/* NOTE: 64-66 used below */
	case OCI_ATTR_AGENT_NAME:			return "OCI_ATTR_AGENT_NAME";		/* agent name */
	case OCI_ATTR_AGENT_ADDRESS:		return "OCI_ATTR_AGENT_ADDRESS";	/* agent address */
	case OCI_ATTR_AGENT_PROTOCOL:		return "OCI_ATTR_AGENT_PROTOCOL";	/* agent protocol */

	case OCI_ATTR_SENDER_ID:			return "OCI_ATTR_SENDER_ID";		/* sender id */
	case OCI_ATTR_ORIGINAL_MSGID:		return "OCI_ATTR_ORIGINAL_MSGID";	/* original message id */

	case OCI_ATTR_QUEUE_NAME:			return "OCI_ATTR_QUEUE_NAME";		/* queue name */
	case OCI_ATTR_NFY_MSGID:			return "OCI_ATTR_NFY_MSGID";		/* message id */
	case OCI_ATTR_MSG_PROP:				return "OCI_ATTR_MSG_PROP";			/* message properties */

	case OCI_ATTR_NUM_DML_ERRORS:		return "OCI_ATTR_NUM_DML_ERRORS";	/* num of errs in array DML */
	case OCI_ATTR_DML_ROW_OFFSET:		return "OCI_ATTR_DML_ROW_OFFSET";	/* row offset in the array */

	case OCI_ATTR_DATEFORMAT:			return "OCI_ATTR_DATEFORMAT";		/* default date format string */
	case OCI_ATTR_BUF_ADDR:				return "OCI_ATTR_BUF_ADDR";			/* buffer address */
	case OCI_ATTR_BUF_SIZE:				return "OCI_ATTR_BUF_SIZE";			/* buffer size */
	case OCI_ATTR_DIRPATH_MODE:			return "OCI_ATTR_DIRPATH_MODE";		/* mode of direct path operation */
	case OCI_ATTR_DIRPATH_NOLOG:		return "OCI_ATTR_DIRPATH_NOLOG";	/* nologging option */
	case OCI_ATTR_DIRPATH_PARALLEL:		return "OCI_ATTR_DIRPATH_PARALLEL";	/* parallel (temp seg) option */
	case OCI_ATTR_NUM_ROWS:				return "OCI_ATTR_NUM_ROWS"; 		/* number of rows in column array */
																			/* NOTE that OCI_ATTR_NUM_COLS is a column*/
																			/* array attribute too.*/
	case OCI_ATTR_COL_COUNT:			return "OCI_ATTR_COL_COUNT";        /* columns of column array*/
																			/*processed so far.       */
	case OCI_ATTR_STREAM_OFFSET:		return "OCI_ATTR_STREAM_OFFSET";	/* str off of last row processed*/
/*	case OCI_ATTR_SHARED_HEAPALLO:		return "";							Shared Heap Allocation Size */

	case OCI_ATTR_SERVER_GROUP:			return "OCI_ATTR_SERVER_GROUP";		/* server group name */

	case OCI_ATTR_MIGSESSION:			return "OCI_ATTR_MIGSESSION"; 		/* migratable session attribute */

	case OCI_ATTR_NOCACHE:				return "OCI_ATTR_NOCACHE";			/* Temporary LOBs */

	case OCI_ATTR_MEMPOOL_SIZE:			return "OCI_ATTR_MEMPOOL_SIZE";		/* Pool Size */
	case OCI_ATTR_MEMPOOL_INSTNAME:		return "OCI_ATTR_MEMPOOL_INSTNAME";	/* Instance name */
	case OCI_ATTR_MEMPOOL_APPNAME:		return "OCI_ATTR_MEMPOOL_APPNAME";	/* Application name */
	case OCI_ATTR_MEMPOOL_HOMENAME:		return "OCI_ATTR_MEMPOOL_HOMENAME";	/* Home Directory name */
	case OCI_ATTR_MEMPOOL_MODEL:		return "OCI_ATTR_MEMPOOL_MODEL";	/* Pool Model (proc,thrd,both)*/
	case OCI_ATTR_MODES:				return "OCI_ATTR_MODES";			/* Modes */

	case OCI_ATTR_SUBSCR_NAME:			return "OCI_ATTR_SUBSCR_NAME";		/* name of subscription */
	case OCI_ATTR_SUBSCR_CALLBACK:		return "OCI_ATTR_SUBSCR_CALLBACK";	/* associated callback */
	case OCI_ATTR_SUBSCR_CTX:			return "OCI_ATTR_SUBSCR_CTX";		/* associated callback context */
	case OCI_ATTR_SUBSCR_PAYLOAD:		return "OCI_ATTR_SUBSCR_PAYLOAD";	/* associated payload */
	case OCI_ATTR_SUBSCR_NAMESPACE:		return "OCI_ATTR_SUBSCR_NAMESPACE"; /* associated namespace */

	case OCI_ATTR_PROXY_CREDENTIALS:	return "OCI_ATTR_PROXY_CREDENTIALS";	/* Proxy user credentials */
	case OCI_ATTR_INITIAL_CLIENT_ROLES:	return "OCI_ATTR_INITIAL_CLIENT_ROLES";	/* Initial client role list */

	case OCI_ATTR_UNK:					return "OCI_ATTR_UNK";				/* unknown attribute */
	case OCI_ATTR_NUM_COLS:				return "OCI_ATTR_NUM_COLS";			/* number of columns */
	case OCI_ATTR_LIST_COLUMNS:			return "OCI_ATTR_LIST_COLUMNS";		/* parameter of the column list */
	case OCI_ATTR_RDBA:					return "OCI_ATTR_RDBA";				/* DBA of the segment header */
	case OCI_ATTR_CLUSTERED:			return "OCI_ATTR_CLUSTERED";		/* whether the table is clustered */
	case OCI_ATTR_PARTITIONED:			return "OCI_ATTR_PARTITIONED";		/* whether the table is partitioned */
	case OCI_ATTR_INDEX_ONLY:			return "OCI_ATTR_INDEX_ONLY";		/* whether the table is index only */
	case OCI_ATTR_LIST_ARGUMENTS:		return "OCI_ATTR_LIST_ARGUMENTS";	/* parameter of the argument list */
	case OCI_ATTR_LIST_SUBPROGRAMS:		return "OCI_ATTR_LIST_SUBPROGRAMS";	/* parameter of the subprogram list */
	case OCI_ATTR_REF_TDO:				return "OCI_ATTR_REF_TDO";			/* REF to the type descriptor */
	case OCI_ATTR_LINK:					return "OCI_ATTR_LINK";				/* the database link name */
	case OCI_ATTR_MIN:					return "OCI_ATTR_MIN";				/* minimum value */
	case OCI_ATTR_MAX:					return "OCI_ATTR_MAX";				/* maximum value */
	case OCI_ATTR_INCR:					return "OCI_ATTR_INCR";				/* increment value */
	case OCI_ATTR_CACHE:				return "OCI_ATTR_CACHE";			/* number of sequence numbers cached */
	case OCI_ATTR_ORDER:				return "OCI_ATTR_ORDER";			/* whether the sequence is ordered */
	case OCI_ATTR_HW_MARK:				return "OCI_ATTR_HW_MARK";			/* high-water mark */
	case OCI_ATTR_TYPE_SCHEMA:			return "OCI_ATTR_TYPE_SCHEMA";		/* type's schema name */
	case OCI_ATTR_TIMESTAMP:			return "OCI_ATTR_TIMESTAMP";		/* timestamp of the object */
	case OCI_ATTR_NUM_ATTRS:			return "OCI_ATTR_NUM_ATTRS";		/* number of sttributes */
	case OCI_ATTR_NUM_PARAMS:			return "OCI_ATTR_NUM_PARAMS";		/* number of parameters */
	case OCI_ATTR_OBJID:				return "OCI_ATTR_OBJID";			/* object id for a table or view */
	case OCI_ATTR_PTYPE:				return "OCI_ATTR_PTYPE";			/* type of info described by */
	case OCI_ATTR_PARAM:				return "OCI_ATTR_PARAM";			/* parameter descriptor */
	case OCI_ATTR_OVERLOAD_ID:			return "OCI_ATTR_OVERLOAD_ID";		/* overload ID for funcs and procs */
	case OCI_ATTR_TABLESPACE:			return "OCI_ATTR_TABLESPACE";		/* table name space */
	case OCI_ATTR_TDO:					return "OCI_ATTR_TDO";				/* TDO of a type */
	case OCI_ATTR_LTYPE:				return "OCI_ATTR_LTYPE";			/* list type */
	case OCI_ATTR_PARSE_ERROR_OFFSET:	return "OCI_ATTR_PARSE_ERROR_OFFSET";/* Parse Error offset */
	case OCI_ATTR_IS_TEMPORARY:			return "OCI_ATTR_IS_TEMPORARY";		/* whether table is temporary */
	case OCI_ATTR_IS_TYPED:				return "OCI_ATTR_IS_TYPED";			/* whether table is typed */
	case OCI_ATTR_DURATION:				return "OCI_ATTR_DURATION";			/* duration of temporary table */
	case OCI_ATTR_IS_INVOKER_RIGHTS:	return "OCI_ATTR_IS_INVOKER_RIGHTS";/* is invoker rights */
	case OCI_ATTR_OBJ_NAME:				return "OCI_ATTR_OBJ_NAME";			/* top level schema obj name */
	case OCI_ATTR_OBJ_SCHEMA:			return "OCI_ATTR_OBJ_SCHEMA";		/* schema name */
	case OCI_ATTR_OBJ_ID:				return "OCI_ATTR_OBJ_ID";			/* top level schema object id */

	case OCI_ATTR_DIRPATH_SORTED_INDEX:	return "OCI_ATTR_DIRPATH_SORTED_INDEX";/* index that data is sorted on */
																			   /* direct path index maint method (see oci8dp.h) */
	case OCI_ATTR_DIRPATH_INDEX_MAINT_METHOD:	return "OCI_ATTR_DIRPATH_INDEX_MAINT_METHOD";/* parallel load: db file, initial and next extent sizes */

	case OCI_ATTR_DIRPATH_FILE:			return "OCI_ATTR_DIRPATH_FILE";		/* DB file to load into */
	case OCI_ATTR_DIRPATH_STORAGE_INITIAL:		return "OCI_ATTR_DIRPATH_STORAGE_INITIAL";	/* initial extent size */
	case OCI_ATTR_DIRPATH_STORAGE_NEXT:	return "OCI_ATTR_DIRPATH_STORAGE_NEXT";	/* next extent size */


	case OCI_ATTR_TRANS_TIMEOUT:		return "OCI_ATTR_TRANS_TIMEOUT";	/* transaction timeout */
	case OCI_ATTR_SERVER_STATUS:		return "OCI_ATTR_SERVER_STATUS";	/* state of the server handle */
	case OCI_ATTR_STATEMENT:			return "OCI_ATTR_STATEMENT"; 		/* statement txt in stmt hdl */
																			/* statement should not be executed in cache*/
	/*case OCI_ATTR_NO_CACHE:			return "";*/
	case OCI_ATTR_DEQCOND:				return "OCI_ATTR_DEQCOND";			/* dequeue condition */
	case OCI_ATTR_RESERVED_2:			return "OCI_ATTR_RESERVED_2";		/* reserved */


	case OCI_ATTR_SUBSCR_RECPT:			return "OCI_ATTR_SUBSCR_RECPT";		/* recipient of subscription */
	case OCI_ATTR_SUBSCR_RECPTPROTO:	return "OCI_ATTR_SUBSCR_RECPTPROTO";/* protocol for recipient */

	/* 8.2 dpapi support of ADTs */
	case OCI_ATTR_DIRPATH_EXPR_TYPE:	return "OCI_ATTR_DIRPATH_EXPR_TYPE";	/* expr type of OCI_ATTR_NAME */

	case OCI_ATTR_DIRPATH_INPUT:		return "OCI_ATTR_DIRPATH_INPUT";	/* input in text or stream format*/
/*	case OCI_DIRPATH_INPUT_TEXT:				return "";
	case OCI_DIRPATH_INPUT_STREAM:				return "";
	case OCI_DIRPATH_INPUT_UNKNOWN:				return "";	*/
	case OCI_ATTR_LDAP_HOST:			return "OCI_ATTR_LDAP_HOST";		/* LDAP host to connect to */
	case OCI_ATTR_LDAP_PORT:			return "OCI_ATTR_LDAP_PORT";		/* LDAP port to connect to */
	case OCI_ATTR_BIND_DN:				return "OCI_ATTR_BIND_DN";			/* bind DN */
	case OCI_ATTR_LDAP_CRED:			return "OCI_ATTR_LDAP_CRED";		/* credentials to connect to LDAP */
	case OCI_ATTR_WALL_LOC:				return "OCI_ATTR_WALL_LOC";			/* client wallet location */
	case OCI_ATTR_LDAP_AUTH:			return "OCI_ATTR_LDAP_AUTH";		/* LDAP authentication method */
	case OCI_ATTR_LDAP_CTX:				return "OCI_ATTR_LDAP_CTX";			/* LDAP administration context DN */
	case OCI_ATTR_SERVER_DNS:			return "OCI_ATTR_SERVER_DNS";		/* list of registration server DNs */

	case OCI_ATTR_DN_COUNT:				return "OCI_ATTR_DN_COUNT";			/* the number of server DNs */
	case OCI_ATTR_SERVER_DN:			return "OCI_ATTR_SERVER_DN";		/* server DN attribute */

	case OCI_ATTR_MAXCHAR_SIZE:			return "OCI_ATTR_MAXCHAR_SIZE";		/* max char size of data */

	case OCI_ATTR_CURRENT_POSITION:		return "OCI_ATTR_CURRENT_POSITION"; /* for scrollable result sets*/

	/* Added to get attributes for ref cursor to statement handle */
	case OCI_ATTR_RESERVED_3:			return "OCI_ATTR_RESERVED_3";		/* reserved */
	case OCI_ATTR_RESERVED_4:			return "OCI_ATTR_RESERVED_4";		/* reserved */
	case OCI_ATTR_DIRPATH_FN_CTX:		return "";							/* fn ctx ADT attrs or args */
	case OCI_ATTR_DIGEST_ALGO:			return "OCI_ATTR_DIRPATH_FN_CTX";	/* digest algorithm */
	case OCI_ATTR_CERTIFICATE:			return "OCI_ATTR_CERTIFICATE";		/* certificate */
	case OCI_ATTR_SIGNATURE_ALGO:		return "OCI_ATTR_SIGNATURE_ALGO";	/* signature algorithm */
	case OCI_ATTR_CANONICAL_ALGO:		return "OCI_ATTR_CANONICAL_ALGO";	/* canonicalization algo. */
	case OCI_ATTR_PRIVATE_KEY:			return "OCI_ATTR_PRIVATE_KEY";		/* private key */
	case OCI_ATTR_DIGEST_VALUE:			return "OCI_ATTR_DIGEST_VALUE";		/* digest value */
	case OCI_ATTR_SIGNATURE_VAL:		return "OCI_ATTR_SIGNATURE_VAL";	/* signature value */
	case OCI_ATTR_SIGNATURE:			return "OCI_ATTR_SIGNATURE";		/* signature */

	/* attributes for setting OCI stmt caching specifics in svchp */
	case OCI_ATTR_STMTCACHESIZE :		return "OCI_ATTR_STMTCACHESIZE";	/* size of the stm cache */

	/* --------------------------- Connection Pool Attributes ------------------ */
	case OCI_ATTR_CONN_NOWAIT:			return "OCI_ATTR_CONN_NOWAIT";
	case OCI_ATTR_CONN_BUSY_COUNT:		return "OCI_ATTR_CONN_BUSY_COUNT";
	case OCI_ATTR_CONN_OPEN_COUNT:		return "OCI_ATTR_CONN_OPEN_COUNT";
	case OCI_ATTR_CONN_TIMEOUT:			return "OCI_ATTR_CONN_TIMEOUT";
	case OCI_ATTR_STMT_STATE:			return "OCI_ATTR_STMT_STATE";
	case OCI_ATTR_CONN_MIN:				return "OCI_ATTR_CONN_MIN";
	case OCI_ATTR_CONN_MAX:				return "OCI_ATTR_CONN_MAX";
	case OCI_ATTR_CONN_INCR:			return "OCI_ATTR_CONN_INCR";

	case OCI_ATTR_DIRPATH_OID:			return "OCI_ATTR_DIRPATH_OID";		/* loading into an OID col */

	case OCI_ATTR_NUM_OPEN_STMTS:		return "OCI_ATTR_NUM_OPEN_STMTS";	/* open stmts in session */
	case OCI_ATTR_DESCRIBE_NATIVE:		return "OCI_ATTR_DESCRIBE_NATIVE";	/* get native info via desc */

	case OCI_ATTR_BIND_COUNT:			return "OCI_ATTR_BIND_COUNT";		/* number of bind postions */
	case OCI_ATTR_HANDLE_POSITION:		return "OCI_ATTR_HANDLE_POSITION";	/* pos of bind/define handle */
	case OCI_ATTR_RESERVED_5:			return "OCI_ATTR_RESERVED_5";		/* reserved */
	case OCI_ATTR_SERVER_BUSY:			return "OCI_ATTR_SERVER_BUSY";		/* call in progress on server*/

	case OCI_ATTR_DIRPATH_SID:			return "OCI_ATTR_DIRPATH_SID";		/* loading into an SID col */
	/* notification presentation for recipient */
	case OCI_ATTR_SUBSCR_RECPTPRES:		return "OCI_ATTR_SUBSCR_RECPTPRES";
	case OCI_ATTR_TRANSFORMATION:		return "OCI_ATTR_TRANSFORMATION"; 	/* AQ message transformation */

	case OCI_ATTR_ROWS_FETCHED:			return "OCI_ATTR_ROWS_FETCHED";		/* rows fetched in last call */

	/* --------------------------- Snapshot attributes ------------------------- */
	case OCI_ATTR_SCN_BASE:				return "OCI_ATTR_SCN_BASE";			/* snapshot base */
	case OCI_ATTR_SCN_WRAP:				return "OCI_ATTR_SCN_WRAP";			/* snapshot wrap */

	/* --------------------------- Miscellaneous attributes --------------------- */
	case OCI_ATTR_RESERVED_6:			return "OCI_ATTR_RESERVED_6";		/* reserved */
	case OCI_ATTR_READONLY_TXN:			return "OCI_ATTR_READONLY_TXN";		/* txn is readonly */
	case OCI_ATTR_RESERVED_7:			return "OCI_ATTR_RESERVED_7";		/* reserved */
	case OCI_ATTR_ERRONEOUS_COLUMN:		return "OCI_ATTR_ERRONEOUS_COLUMN"; /* position of erroneous col */
	case OCI_ATTR_RESERVED_8:			return "OCI_ATTR_RESERVED_8";		/* reserved */

	/* -------------------- 8.2 dpapi support of ADTs continued ---------------- */
	case OCI_ATTR_DIRPATH_OBJ_CONSTR:	return "OCI_ATTR_DIRPATH_OBJ_CONSTR"; /* obj type of subst obj tbl */

	/************************FREE attribute     207      *************************/
	/************************FREE attribute     208      *************************/
	case OCI_ATTR_ENV_UTF16:			return "OCI_ATTR_ENV_UTF16";		/* is env in utf16 mode? */
	case OCI_ATTR_RESERVED_9:			return "OCI_ATTR_RESERVED_9";		/* reserved for TMZ */
	case OCI_ATTR_RESERVED_10:			return "OCI_ATTR_RESERVED_10";		/* reserved */

	/* Attr to allow setting of the stream version PRIOR to calling Prepare */
	case OCI_ATTR_DIRPATH_STREAM_VERSION:	return "OCI_ATTR_DIRPATH_STREAM_VERSION";	/* version of the stream*/
/*	case OCI_ATTR_RESERVED_11:				return "OCI_ATTR_RESERVED_11";	reserved */

	case OCI_ATTR_RESERVED_12:			return "OCI_ATTR_RESERVED_12";		/* reserved */
	case OCI_ATTR_RESERVED_13:			return "OCI_ATTR_RESERVED_13";		/* reserved */

	/* OCI_ATTR_RESERVED_14 */
#ifdef OCI_ATTR_RESERVED_15
	case OCI_ATTR_RESERVED_15:			return "OCI_ATTR_RESERVED_15";		/* reserved */
#endif
#ifdef OCI_ATTR_RESERVED_16
	case OCI_ATTR_RESERVED_16:			return "OCI_ATTR_RESERVED_16";		/* reserved */
#endif

	}
	sv = sv_2mortal(newSViv((IV)attr));
	return SvPV(sv,PL_na);
}

/*used to look up the name of a fetchtype constant
  used only for debugging */
char *
oci_fetch_options(ub4 fetchtype)
{
	dTHX;
	SV *sv;
	switch (fetchtype) {
	/* fetch options */
		case OCI_FETCH_CURRENT:		return "OCI_FETCH_CURRENT";
		case OCI_FETCH_NEXT:		return "OCI_FETCH_NEXT";
		case OCI_FETCH_FIRST:		return "OCI_FETCH_FIRST";
		case OCI_FETCH_LAST:		return "OCI_FETCH_LAST";
		case OCI_FETCH_PRIOR:		return "OCI_FETCH_PRIOR";
		case OCI_FETCH_ABSOLUTE:	return "OCI_FETCH_ABSOLUTE";
		case OCI_FETCH_RELATIVE:	return "OCI_FETCH_RELATIVE";
	}
	sv = sv_2mortal(newSViv((IV)fetchtype));
	return SvPV(sv,PL_na);
}




static sb4
oci_error_get(imp_xxh_t *imp_xxh,
              OCIError *errhp, sword status, char *what, SV *errstr, int debug)
{
	dTHX;
	text errbuf[1024];
	ub4 recno = 0;
	sb4 errcode = 0;
	sb4 eg_errcode = 0;
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
		&& recno < 100) {
		if (debug >= 4 || recno>1/*XXX temp*/)
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


int
oci_error_err(SV *h, OCIError *errhp, sword status, char *what, sb4 force_err)
{

	dTHX;
	D_imp_xxh(h);
	sb4 errcode;
        int utf8_is_implied = 0;
	SV *errstr_sv = sv_newmortal();
	SV *errcode_sv = sv_newmortal();
	errcode = oci_error_get(imp_xxh, errhp, status, what, errstr_sv,
                            DBIc_DBISTATE(imp_xxh)->debug);

        if(DBIc_TYPE(imp_xxh) == DBIt_ST)
        {
            imp_sth_t * imp_sth = (imp_sth_t*)imp_xxh;
            D_imp_dbh_from_sth;
            utf8_is_implied = CSFORM_IMPLIES_UTF8(imp_dbh, SQLCS_IMPLICIT);
        }
        else if(DBIc_TYPE(imp_xxh) == DBIt_DB)
            utf8_is_implied = CSFORM_IMPLIES_UTF8((imp_dbh_t *)imp_xxh, SQLCS_IMPLICIT);
        else if(DBIc_TYPE(imp_xxh) == DBIt_DR)
            utf8_is_implied = CSFORM_IMPLIES_UTF8((imp_drh_t *)imp_xxh, SQLCS_IMPLICIT);

	if (utf8_is_implied) {
#ifdef sv_utf8_decode
            sv_utf8_decode(errstr_sv);
#else
            SvUTF8_on(errstr_sv);
#endif
	}

	/* DBIc_ERR *must* be SvTRUE (for RaiseError etc), some */
	/* errors, like OCI_INVALID_HANDLE, don't set errcode. */
	if (force_err)
		errcode = force_err;
	if (status == OCI_SUCCESS_WITH_INFO)
		errcode = 0; /* record as a "warning" for DBI>=1.43 */
	else if (errcode == 0)
		errcode = (status != 0) ? status : -10000;

	sv_setiv(errcode_sv, errcode);
	DBIh_SET_ERR_SV(h, imp_xxh, errcode_sv, errstr_sv, &PL_sv_undef, &PL_sv_undef);
	return 0; /* always returns 0 */

}


char *
ora_sql_error(imp_sth_t *imp_sth, char *msg)
{
	dTHX;
#ifdef OCI_ATTR_PARSE_ERROR_OFFSET
	D_imp_dbh_from_sth;
	SV  *msgsv, *sqlsv;
	char buf[99];
	sword status = 0;
	ub2 parse_error_offset = 0;
	OCIAttrGet_stmhp_stat(imp_sth, &parse_error_offset, 0,
						  OCI_ATTR_PARSE_ERROR_OFFSET, status);
	imp_dbh->parse_error_offset = parse_error_offset;
	if (!parse_error_offset)
		return msg;
	sprintf(buf,"error possibly near <*> indicator at char %d in '",
		parse_error_offset);
	msgsv = sv_2mortal(newSVpv(buf,0));
	sqlsv = sv_2mortal(newSVpv(imp_sth->statement,0));
	sv_insert(sqlsv, parse_error_offset, 0, "<*>", 3);
	sv_catsv(msgsv, sqlsv);
	sv_catpv(msgsv, "'");
	return SvPV(msgsv,PL_na);
#else
	imp_sth = imp_sth; /* not unused */
	return msg;
#endif
}


void *
oci_db_handle(imp_dbh_t *imp_dbh, int handle_type, int flags)
{
	dTHX;
	 switch(handle_type) {
	 	case OCI_HTYPE_ENV:		return imp_dbh->envhp;
	 	case OCI_HTYPE_ERROR:	return imp_dbh->errhp;
	 	case OCI_HTYPE_SERVER:	return imp_dbh->srvhp;
	 	case OCI_HTYPE_SVCCTX:	return imp_dbh->svchp;
	 	case OCI_HTYPE_SESSION:	return imp_dbh->seshp;
	 	/*case OCI_HTYPE_AUTHINFO:return imp_dbh->authp;*/
	 }
	 croak("Can't get OCI handle type %d from DBI database handle", handle_type);
	 if( flags ) {/* For GCC not to warn on unused parameter */}
	 /* satisfy compiler warning, even though croak will never return */
	 return 0;
}

void *
oci_st_handle(imp_sth_t *imp_sth, int handle_type, int flags)
{
	dTHX;
	 switch(handle_type) {
	 	case OCI_HTYPE_ENV:		return imp_sth->envhp;
		case OCI_HTYPE_ERROR:	return imp_sth->errhp;
	 	case OCI_HTYPE_SERVER:	return imp_sth->srvhp;
	 	case OCI_HTYPE_SVCCTX:	return imp_sth->svchp;
	 	case OCI_HTYPE_STMT:	return imp_sth->stmhp;
	 }
	 croak("Can't get OCI handle type %d from DBI statement handle", handle_type);
	 if( flags ) {/* For GCC not to warn on unused parameter */}
	 /* satisfy compiler warning, even though croak will never return */
	 return 0;
}


int
dbd_st_prepare(SV *sth, imp_sth_t *imp_sth, char *statement, SV *attribs)
{
	dTHX;
	D_imp_dbh_from_sth;
	sword status 		 = 0;
	IV  ora_piece_size	 = 0;
	IV  ora_pers_lob	 = 0;
	IV  ora_piece_lob	 = 0;
	IV  ora_clbk_lob	 = 0;
	int ora_check_sql 	 = 1;	/* to force a describe to check SQL	*/
	IV  ora_placeholders = 1;	/* find and handle placeholders */
	/* XXX we set ora_check_sql on for now to force setup of the	*/
	/* row cache. Change later to set up row cache using just a	*/
	/* a memory size, perhaps also default $RowCacheSize to a	*/
	/* negative value. OCI_ATTR_PREFETCH_MEMORY */


	if (!DBIc_ACTIVE(imp_dbh)) {
		oci_error(sth, NULL, OCI_ERROR, "Database disconnected");
		return 0;
	}

	imp_dbh->parse_error_offset = 0;

	imp_sth->done_desc = 0;
	imp_sth->get_oci_handle = oci_st_handle;

	if (DBIc_COMPAT(imp_sth)) {
		static SV *ora_pad_empty;
		if (!ora_pad_empty) {
			ora_pad_empty= perl_get_sv("Oraperl::ora_pad_empty", GV_ADDMULTI);
			if (!SvOK(ora_pad_empty) && getenv("ORAPERL_PAD_EMPTY"))
				sv_setiv(ora_pad_empty, atoi(getenv("ORAPERL_PAD_EMPTY")));
		}
		imp_sth->ora_pad_empty = (SvOK(ora_pad_empty)) ? SvIV(ora_pad_empty) : 0;
	}

	imp_sth->auto_lob = 1;
	imp_sth->exe_mode  = OCI_DEFAULT;

	if (attribs) {
		SV **svp;
		IV ora_auto_lob = 1;
		DBD_ATTRIB_GET_IV(  attribs, "ora_placeholders", 16, svp, ora_placeholders);
		DBD_ATTRIB_GET_IV(  attribs, "ora_auto_lob", 12, svp, ora_auto_lob);
		DBD_ATTRIB_GET_IV(  attribs, "ora_pers_lob", 12, svp, ora_pers_lob);
		DBD_ATTRIB_GET_IV(  attribs, "ora_clbk_lob", 12, svp, ora_clbk_lob);
		DBD_ATTRIB_GET_IV(  attribs, "ora_piece_lob", 13, svp, ora_piece_lob);
		DBD_ATTRIB_GET_IV(  attribs, "ora_piece_size", 14, svp, ora_piece_size);

		imp_sth->auto_lob	= (ora_auto_lob) ? 1 : 0;
		imp_sth->pers_lob	= (ora_pers_lob) ? 1 : 0;
		imp_sth->clbk_lob 	= (ora_clbk_lob) ? 1 : 0;
		imp_sth->piece_lob	= (ora_piece_lob) ? 1 : 0;
		imp_sth->piece_size	= (ora_piece_size) ? ora_piece_size : 0;
		imp_sth->prefetch_rows 	= 0;
		imp_sth->prefetch_memory= 0;
		/* ora_check_sql only works for selects owing to Oracle behaviour */
		DBD_ATTRIB_GET_IV(  attribs, "ora_check_sql", 13, svp, ora_check_sql);
		DBD_ATTRIB_GET_IV(  attribs, "ora_exe_mode", 12, svp, imp_sth->exe_mode);
		DBD_ATTRIB_GET_IV(  attribs, "ora_prefetch_memory",  19, svp, imp_sth->prefetch_memory);
		DBD_ATTRIB_GET_IV(  attribs, "ora_prefetch_rows",  17, svp, imp_sth->prefetch_rows);
		DBD_ATTRIB_GET_IV(  attribs, "ora_row_cache_off",  17, svp, imp_sth->row_cache_off);
		DBD_ATTRIB_GET_IV(  attribs, "ora_verbose",  11, svp, dbd_verbose);
		DBD_ATTRIB_GET_IV(  attribs, "ora_oci_success_warn",  20, svp, oci_warn);
		DBD_ATTRIB_GET_IV(  attribs, "ora_objects",  11, svp, ora_objects);
		DBD_ATTRIB_GET_IV(  attribs, "ora_ncs_buff_mtpl",  17, svp,ora_ncs_buff_mtpl);
        DBD_ATTRIB_GET_IV(  attribs, "RowCacheSize",12,svp, imp_sth->RowCacheSize);

		if (!dbd_verbose)
			DBD_ATTRIB_GET_IV(  attribs, "dbd_verbose",  11, svp, dbd_verbose);
	}


 	/* scan statement for '?', ':1' and/or ':foo' style placeholders	*/
	if (ora_placeholders)
		dbd_preparse(imp_sth, statement);
	else imp_sth->statement = savepv(statement);

	imp_sth->envhp = imp_dbh->envhp;
	imp_sth->errhp = imp_dbh->errhp;
	imp_sth->srvhp = imp_dbh->srvhp;
	imp_sth->svchp = imp_dbh->svchp;



	OCIHandleAlloc_ok(imp_dbh, imp_dbh->envhp, &imp_sth->stmhp, OCI_HTYPE_STMT, status);
	OCIStmtPrepare_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp,
			(text*)imp_sth->statement, (ub4)strlen(imp_sth->statement),
			OCI_NTV_SYNTAX, OCI_DEFAULT, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIStmtPrepare");
		OCIHandleFree_log_stat(imp_sth, imp_sth->stmhp, OCI_HTYPE_STMT, status);

		return 0;
	}


	OCIAttrGet_stmhp_stat(imp_sth, &imp_sth->stmt_type, 0, OCI_ATTR_STMT_TYPE, status);

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	dbd_st_prepare'd sql %s ( auto_lob%d, check_sql%d)\n",
			oci_stmt_type_name(imp_sth->stmt_type),
			imp_sth->auto_lob, ora_check_sql);

	DBIc_IMPSET_on(imp_sth);

	if (ora_check_sql) {
		if (!dbd_describe(sth, imp_sth))
			return 0;
	}

	return 1;
}


sb4
dbd_phs_in(dvoid *octxp, OCIBind *bindp, ub4 iter, ub4 index,
		  dvoid **bufpp, ub4 *alenp, ub1 *piecep, dvoid **indpp)
{
	dTHX;
	phs_t *phs = (phs_t*)octxp;
	STRLEN phs_len;
	AV *tuples_av;
	SV *sv;
	AV *av;
	SV **sv_p;
	if( bindp ){ /* For GCC not to warn on unused parameter*/ }

	tuples_av = phs->imp_sth->bind_tuples;
	if(tuples_av) {
		/* NOTE: we already checked the validity in ora_st_bind_for_array_exec(). */
		sv_p = av_fetch(tuples_av, phs->imp_sth->rowwise ? (int)iter : phs->idx, 0);
		av = (AV*)SvRV(*sv_p);
		sv_p = av_fetch(av, phs->imp_sth->rowwise ? phs->idx : (int)iter, 0);
		sv = *sv_p;
		if(SvOK(sv)) {
			*bufpp = SvPV(sv, phs_len);
			phs->alen = (phs->alen_incnull) ? phs_len+1 : phs_len;
			phs->indp = 0;
		}
		else {
			*bufpp = SvPVX(sv);
			phs->alen = 0;
			phs->indp = -1;
		}
	}
	else
		if (phs->desc_h) {
			*bufpp  = phs->desc_h;
			phs->alen = 0;
			phs->indp = 0;
		}
		else
			if (SvOK(phs->sv)) {
				*bufpp  = SvPV(phs->sv, phs_len);
				phs->alen = (phs->alen_incnull) ? phs_len+1 : phs_len;;
				phs->indp = 0;
			}
			else {
				*bufpp  = SvPVX(phs->sv);	/* not actually used? */
				phs->alen = 0;
				phs->indp = -1;
			}
	*alenp  = phs->alen;
	*indpp  = &phs->indp;
	*piecep = OCI_ONE_PIECE;
	/* MJE commented out as we are avoiding DBIS now but as this is
	   an Oracle callback there is no way to pass something non
	   OCI into this func.

	if (DBIS->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(DBILOGFP, "		in  '%s' [%lu,%lu]: len %2lu, ind %d%s, value=%s\n",
			phs->name, ul_t(iter), ul_t(index), ul_t(phs->alen), phs->indp,
			(phs->desc_h) ? " via descriptor" : "",neatsvpv(phs->sv,10));
	*/
	if (!tuples_av && (index > 0 || iter > 0))
		croak(" Arrays and multiple iterations not currently supported by DBD::Oracle (in %d/%d)", index,iter);

	return OCI_CONTINUE;
}

/*
``Binding and Defining''

Binding RETURNING...INTO variables

As mentioned in the previous section, an OCI application implements the placeholders in the RETURNING clause as
pure OUT bind variables. An application must adhere to the following rules when working with these bind variables:

  1.Bind RETURNING clause placeholders in OCI_DATA_AT_EXEC mode using OCIBindByName() or
	OCIBindByPos(), followed by a call to OCIBindDynamic() for each placeholder.

	Note: The OCI only supports the callback mechanism for RETURNING clause binds. The polling mechanism is
	not supported.

  2.When binding RETURNING clause placeholders, you must supply a valid out bind function as the ocbfp
	parameter of the OCIBindDynamic() call. This function must provide storage to hold the returned data.
  3.The icbfp parameter of OCIBindDynamic() call should provide a "dummy" function which returns NULL values
	when called.
  4.The piecep parameter of OCIBindDynamic() must be set to OCI_ONE_PIECE.
  5.No duplicate binds are allowed in a DML statement with a RETURNING clause (i.e., no duplication between bind
	variables in the DML section and the RETURNING section of the statement).

When a callback function is called, the OCI_ATTR_ROWS_RETURNED attribute of the bind handle tells the
application the number of rows being returned in that particular iteration. Thus, when the callback is called the first
time in a particular iteration (i.e., index=0), the user can allocate space for all the rows which will be returned for that
bind variable. When the callback is called subsequently (with index>0) within the same iteration, the user can merely
increment the buffer pointer to the correct memory within the allocated space to retrieve the data.

Every bind handle has a OCI_ATTR_MAXDATA_SIZE attribute. This attribute specifies the number of bytes to be
allocated on the server to accommodate the client-side bind data after any necessary character set conversions.

	Note: Character set conversions performed when data is sent to the server may result in the data expanding or
	contracting, so its size on the client may not be the same as its size on the server.

An application will typically set OCI_ATTR_MAXDATA_SIZE to the maximum size of the column or the size of the
PL/SQL variable, depending on how it is used. Oracle issues an error if OCI_ATTR_MAXDATA_SIZE is not a large
enough value to accommodate the data after conversion, and the operation will fail.
*/

sb4
dbd_phs_out(dvoid *octxp, OCIBind *bindp,
	ub4 iter,	/* execution itteration (0...)	*/
	ub4 index,	/* array index (0..)		*/
	dvoid **bufpp,	/* A pointer to a buffer to write the bind value/piece.	*/
	ub4 **alenpp,	/* A pointer to a storage for OCI to fill in the size	*/
			/* of the bind value/piece after it has been read.	*/
	ub1 *piecep,	/* */
	dvoid **indpp,	/* Return a pointer to contain the indicator value which either an sb2	*/
			/* value or a pointer to an indicator structure for named data types.	*/
	ub2 **rcodepp)	/* Returns a pointer to contains the return code.	*/
{
	dTHX;
	phs_t *phs = (phs_t*)octxp;	/* context */
	/*imp_sth_t *imp_sth = phs->imp_sth;*/
	if( bindp ) { /* For GCC not to warn on unused parameter */ }

	if (phs->desc_h) { /* a  descriptor if present  (LOBs etc)*/
		*bufpp  = phs->desc_h;
		phs->alen = 0;

	}
	else {
		SV *sv = phs->sv;

		if (SvTYPE(sv) == SVt_RV && SvTYPE(SvRV(sv)) == SVt_PVAV) {
			sv = *av_fetch((AV*)SvRV(sv), (IV)iter, 1);
			if (!SvOK(sv))
				sv_setpv(sv,"");
		}

        *bufpp = SvGROW(sv, (size_t)(((phs->maxlen < 28) ? 28 : phs->maxlen)));
		phs->alen = SvLEN(sv);	/* max buffer size now, actual data len later */

	}
	*alenpp = &phs->alen;
	*indpp  = &phs->indp;
	*rcodepp= &phs->arcode;
    /* MJE commented out as we are avoiding DBIS now but as this is
       an Oracle callback there is no way to pass something non
       OCI into this func.

	if (DBIS->debug >= 3 || dbd_verbose >= 3 )
 		PerlIO_printf(DBILOGFP, "		out '%s' [%ld,%ld]: alen %2ld, piece %d%s\n",
			phs->name, ul_t(iter), ul_t(index), ul_t(phs->alen), *piecep,
			(phs->desc_h) ? " via descriptor" : "");
    */
	*piecep = OCI_ONE_PIECE;
	return OCI_CONTINUE;
}

/* --------------------------------------------------------------
	Fetch callback fill buffers.
	Finaly figured out how this fucntion works
	Seems it is like this. The function inits and then fills the
	buffer (fb_ary->abuf) with the data from the select until it
	either runs out of data or its piece size is reached
	(fb_ary->bufl).  If its piece size is reached it then goes and gets
	the the next piece and sets *piecep ==OCI_NEXT_PIECE at this point
	I take the data in the buffer and memcpy it onto my buffer
	(fb_ary->cb_abuf). This will go on until it runs out of full pieces
	so when it returns to back to the fetch I add what remains in
	(fb_ary->bufl) (the last piece) and memcpy onto my  buffer (fb_ary->cb_abuf)
	to get it all.  I also take set fb_ary->cb_abuf back to empty just
	to keep things clean
 -------------------------------------------------------------- */
sb4
presist_lob_fetch_cbk(dvoid *octxp, OCIDefine *dfnhp, ub4 iter, dvoid **bufpp,
					  ub4 **alenpp, ub1 *piecep, dvoid **indpp, ub2 **rcpp)
{
	dTHX;
	imp_fbh_t	*fbh =(imp_fbh_t*)octxp;
	fb_ary_t	*fb_ary;
	fb_ary	= fbh->fb_ary;
	*bufpp	= (dvoid *) fb_ary->abuf;
	*alenpp	= &fb_ary->bufl;
	*indpp	= (dvoid *) fb_ary->aindp;
	*rcpp	= fb_ary->arcode;


	if (dbd_verbose >= 5 ) {
		PerlIO_printf(DBILOGFP, " In presist_lob_fetch_cbk\n");
	}

	if ( *piecep ==OCI_NEXT_PIECE ){/*more than one piece*/

		memcpy(fb_ary->cb_abuf+fb_ary->piece_count*fb_ary->bufl,fb_ary->abuf,fb_ary->bufl );
	/*as we will be using both blobs and clobs we have to use
	  pointer arithmetic to get the values right.  in this case we simply
	  copy all of the memory of the buff into the cb buffer starting
	  at the piece count * the  buffer length
	  */

		fb_ary->piece_count++;/*used to tell me how many pieces I have, Might be able to use aindp for this?*/

	}


	return OCI_CONTINUE;

}

/* TAF or Transparent Application Failoever callback
   Works like this.  The fuction below is registered on the server,
   when the server is set up to use it, when an exe is called (not sure about other server round trips)
   and the server fails tt should get into this cbk error below.
   It will wait X seconds and then try to reconnect (up to n times if that is the users choice)
   That is how I see it working */

sb4
taf_cbk(dvoid *svchp, dvoid *envhp, dvoid *fo_ctx,ub4 fo_type, ub4 fo_event )
{
	dTHX;
    int return_count;
    int ret;
	taf_callback_t *cb =(taf_callback_t*)fo_ctx;

	dSP;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSViv(fo_event)));
	XPUSHs(sv_2mortal(newSViv(fo_type)));
    XPUSHs(SvRV(cb->dbh_ref));

	PUTBACK;
	return_count = call_sv(cb->function, G_SCALAR);

    SPAGAIN;

    if (return_count != 1)
        croak("Expected one scalar back from taf handler");

    ret = POPi;

	switch (fo_event){

		case OCI_FO_BEGIN:
		case OCI_FO_ABORT:
		case OCI_FO_END:
		case OCI_FO_REAUTH:
		{
			break;
		}
		case OCI_FO_ERROR:
		{
            if (ret == OCI_FO_RETRY) {
                return OCI_FO_RETRY;
            }
			break;
		}

		default:
		{
			break;
		}
	}
    PUTBACK;

	return 0;
}


sb4
reg_taf_callback(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX;
	OCIFocbkStruct 	tafailover;
	sword 			status;

    imp_dbh->taf_ctx.function = imp_dbh->taf_function;
    imp_dbh->taf_ctx.dbh_ref = newRV_inc(dbh);

	if (dbd_verbose >= 5 ) {
  		PerlIO_printf(DBIc_LOGPIO(imp_dbh), " In reg_taf_callback\n");
	}

/* set the context up as a pointer to the taf callback struct*/
	tafailover.fo_ctx = &imp_dbh->taf_ctx;
	tafailover.callback_function = &taf_cbk;

/* register the callback */
	OCIAttrSet_log_stat(imp_dbh, imp_dbh->srvhp, (ub4) OCI_HTYPE_SERVER,
                        (dvoid *) &tafailover, (ub4) 0,
                        (ub4) OCI_ATTR_FOCBK, imp_dbh->errhp, status);

	return status;
}

#ifdef UTF8_SUPPORT
/* How many bytes are n utf8 chars in buffer */
static ub4
ora_utf8_to_bytes (ub1 *buffer, ub4 chars_wanted, ub4 max_bytes)
{
	dTHX;
	ub4 i = 0;
	while (i < max_bytes && (chars_wanted-- > 0)) {
		i += UTF8SKIP(&buffer[i]);
	}
	return (i < max_bytes)? i : max_bytes;
}


#if 0 /* save this for later just in case... */
/* Given the 5.6.0 implementation of utf8 handling in perl,
 * avoid setting the UTF8 flag as much as possible. Almost
 * every binary operator in Perl will do conversions when
 * strings marked as UTF8 are involved.
 * Maybe setting the flag should be default in Japan or
 * Europe? Deduce that from NLS_LANG? Possibly...
 */

int
set_utf8(SV *sv) {
	ub1 *c;
	for (c = (ub1*)SvPVX(sv); c < (ub1*)SvEND(sv); c++) {
		if (*c & 0x80) {
			SvUTF8_on(sv);
			return 1;
		}
	}
	return 0;
}
#endif
#endif

/* PerlIO_printf(DBILOGFP, "lab datalen=%d long_readlen=%d bytelen=%d\n" ,datalen ,imp_sth->long_readlen, bytelen ); */
static int	/* LONG and LONG RAW */
fetch_func_varfield(SV *sth, imp_fbh_t *fbh, SV *dest_sv)
{
	dTHX;
	D_imp_sth(sth);
	D_imp_dbh_from_sth ;
	D_imp_drh_from_dbh ;
	fb_ary_t *fb_ary = fbh->fb_ary;
	char *p = (char*)&fb_ary->abuf[0];
	ub4 datalen = *(ub4*)p;	 /* XXX alignment ? */
	p += 4;

#ifdef UTF8_SUPPORT
	if (fbh->ftype == 94) {
		if (datalen > imp_sth->long_readlen) {
			ub4 bytelen = ora_utf8_to_bytes((ub1*)p, (ub4)imp_sth->long_readlen, datalen);

			if (bytelen < datalen ) {	/* will be truncated */
				int oraperl = DBIc_COMPAT(imp_sth);
				if (DBIc_has(imp_sth,DBIcf_LongTruncOk) || (oraperl && SvIV(imp_drh->ora_trunc))) {
					/* user says truncation is ok */
					/* Oraperl recorded the truncation in ora_errno so we	*/
					/* so also but only for Oraperl mode handles.		*/
					if (oraperl) sv_setiv(DBIc_ERR(imp_sth), 1406);
				} else {
					char buf[300];
					sprintf(buf,"fetching field %d of %d. LONG value truncated from %lu to %lu. %s",
						fbh->field_num+1, DBIc_NUM_FIELDS(imp_sth), ul_t(datalen), ul_t(bytelen),
						"DBI attribute LongReadLen too small and/or LongTruncOk not set");
					oci_error_err(sth, NULL, OCI_ERROR, buf, 24345); /* appropriate ORA error number */
					sv_set_undef(dest_sv);
					return 0;
				}

                if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "		fetching field %d of %d. LONG value truncated from "
                    "%lu to %lu.\n",
					fbh->field_num+1, DBIc_NUM_FIELDS(imp_sth),
					ul_t(datalen), ul_t(bytelen));
					datalen = bytelen;
			}
	}
	sv_setpvn(dest_sv, p, (STRLEN)datalen);
	if (CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform))
		SvUTF8_on(dest_sv);
	} else {
#else
	{
#endif
	sv_setpvn(dest_sv, p, (STRLEN)datalen);
	}

	return 1;
}

static void
fetch_cleanup_rset(SV *sth, imp_fbh_t *fbh)
{
	dTHX;
    D_imp_sth(sth);
	SV *sth_nested = (SV *)fbh->special;
	fbh->special = NULL;

	if( sth ) { /* For GCC not to warn on unused parameter */ }
	if (sth_nested) {
	dTHR;
	D_impdata(imp_sth_nested, imp_sth_t, sth_nested);
		int fields = DBIc_NUM_FIELDS(imp_sth_nested);
	int i;
	for(i=0; i < fields; ++i) {
		imp_fbh_t *fbh_nested = &imp_sth_nested->fbh[i];
		if (fbh_nested->fetch_cleanup)
		fbh_nested->fetch_cleanup(sth_nested, fbh_nested);
	}
	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
			"	fetch_cleanup_rset - deactivating handle %s (defunct nested cursor)\n",
						neatsvpv(sth_nested, 0));

	DBIc_ACTIVE_off(imp_sth_nested);
	SvREFCNT_dec(sth_nested);
	}
}

static int
fetch_func_rset(SV *sth, imp_fbh_t *fbh, SV *dest_sv)
{
	dTHX;
	OCIStmt *stmhp_nested = ((OCIStmt **)fbh->fb_ary->abuf)[0];
	dTHR;
	D_imp_sth(sth);
	D_imp_dbh_from_sth;
	dSP;
	HV *init_attr = newHV();
	int count;

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	fetch_func_rset - allocating handle for cursor nested within %s ...\n",
            neatsvpv(sth, 0));

	ENTER; SAVETMPS; PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV((SV*)DBIc_MY_H(imp_dbh))));
	XPUSHs(sv_2mortal(newRV((SV*)init_attr)));
	PUTBACK;
	count = perl_call_pv("DBI::_new_sth", G_ARRAY);
	SPAGAIN;
	if (count != 2)
		croak("panic: DBI::_new_sth returned %d values instead of 2", count);

	if(POPs){} /* For GCC not to warn on unused result */

	sv_setsv(dest_sv, POPs);
	SvREFCNT_dec(init_attr);
	PUTBACK; FREETMPS; LEAVE;

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	fetch_func_rset - ... allocated %s for nested cursor\n",
            neatsvpv(dest_sv, 0));

	fbh->special = (void *)newSVsv(dest_sv);

	{
		D_impdata(imp_sth_nested, imp_sth_t, dest_sv);
		imp_sth_nested->envhp = imp_sth->envhp;
		imp_sth_nested->errhp = imp_sth->errhp;
		imp_sth_nested->srvhp = imp_sth->srvhp;
		imp_sth_nested->svchp = imp_sth->svchp;

		imp_sth_nested->stmhp = stmhp_nested;
		imp_sth_nested->nested_cursor = 1;
		imp_sth_nested->stmt_type = OCI_STMT_SELECT;

		DBIc_IMPSET_on(imp_sth_nested);
		DBIc_ACTIVE_on(imp_sth_nested);  /* So describe won't do an execute */

		if (!dbd_describe(dest_sv, imp_sth_nested))
			return 0;
	}

	return 1;
}
/* ------ */


int
dbd_rebind_ph_rset(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;

	if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	 dbd_rebind_ph_rset phs->is_inout=%d\n",
            phs->is_inout);

/* Only do this part for inout cursor refs because pp_exec_rset only gets called for all the output params */
	if (phs->is_inout) {
		phs->out_prepost_exec = pp_exec_rset;
		return 2;	/* OCI bind done */
	}
	else {
	/* Call a special rebinder for cursor ref "in" params */
		return(pp_rebind_ph_rset_in(sth, imp_sth, phs));
	}
}


/* ------ */
static int
fetch_lob(SV *sth, imp_sth_t *imp_sth, OCILobLocator* lobloc, int ftype, SV *dest_sv, char *name);

static int
lob_phs_post_execute(SV *sth, imp_sth_t *imp_sth, phs_t *phs, int pre_exec)
{
	dTHX;
	if (pre_exec)
		return 1;
	/* fetch PL/SQL LOB data */
	if (imp_sth->auto_lob && (
		imp_sth->stmt_type == OCI_STMT_BEGIN ||
		imp_sth->stmt_type == OCI_STMT_DECLARE )) {
		return fetch_lob(sth, imp_sth, (OCILobLocator*) phs->desc_h, phs->ftype, phs->sv, phs->name);
	}

	sv_setref_pv(phs->sv, "OCILobLocatorPtr", (void*)phs->desc_h);

	return 1;
}

int
dbd_rebind_ph_lob(SV *sth, imp_sth_t *imp_sth, phs_t *phs)
{
	dTHX;
	D_imp_dbh_from_sth ;
	sword status;
	ub4 lobEmpty = 0;
    if (phs->desc_h && phs->desc_t == OCI_DTYPE_LOB)
		ora_free_templob(sth, imp_sth, (OCILobLocator*)phs->desc_h);

	if (!phs->desc_h) {
		++imp_sth->has_lobs;
		phs->desc_t = OCI_DTYPE_LOB;
		OCIDescriptorAlloc_ok(imp_sth, imp_sth->envhp,
				&phs->desc_h, phs->desc_t);
	}

	OCIAttrSet_log_stat(imp_sth, phs->desc_h, phs->desc_t,
			&lobEmpty, 0, OCI_ATTR_LOBEMPTY, imp_sth->errhp, status);

	if (status != OCI_SUCCESS)
		return oci_error(sth, imp_sth->errhp, status, "OCIAttrSet OCI_ATTR_LOBEMPTY");

	if (!SvPOK(phs->sv)) {	 /* normalizations for special cases	 */
		if (SvOK(phs->sv)) {	/* ie a number, convert to string ASAP  */
			if (!(SvROK(phs->sv) && phs->is_inout))
				sv_2pv(phs->sv, &PL_na);
		}
		else { /* ensure we're at least an SVt_PV (so SvPVX etc work)	 */
			(void)SvUPGRADE(phs->sv, SVt_PV);
		}
	}

	phs->indp	= (SvOK(phs->sv)) ? 0 : -1;
	phs->progv  = (char*)&phs->desc_h;
	phs->maxlen = sizeof(OCILobLocator*);

	if (phs->is_inout)
		phs->out_prepost_exec = lob_phs_post_execute;
	/* accept input LOBs */

	if (sv_isobject(phs->sv) && sv_derived_from(phs->sv, "OCILobLocatorPtr")) {

		OCILobLocator *src;
		OCILobLocator **dest;
		src = INT2PTR(OCILobLocator *, SvIV(SvRV(phs->sv)));
		dest = (OCILobLocator **) phs->progv;

		OCILobLocatorAssign_log_stat(imp_dbh, imp_dbh->svchp, imp_sth->errhp, src, dest, status);
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobLocatorAssign");
			return 0;
		}
	}

	/* create temporary LOB for PL/SQL placeholder */
	else if (imp_sth->stmt_type == OCI_STMT_BEGIN ||
		imp_sth->stmt_type == OCI_STMT_DECLARE) {
		ub4 amtp;

		(void)SvUPGRADE(phs->sv, SVt_PV);

		amtp = SvCUR(phs->sv);		/* XXX UTF8? */

		/* Create a temp lob for non-empty string */

		if (amtp > 0) {
			ub1 lobtype = (phs->ftype == 112 ? OCI_TEMP_CLOB : OCI_TEMP_BLOB);
			OCILobCreateTemporary_log_stat(imp_dbh, imp_dbh->svchp, imp_sth->errhp,
				(OCILobLocator *) phs->desc_h, (ub2) OCI_DEFAULT,
				(ub1) OCI_DEFAULT, lobtype, TRUE, OCI_DURATION_SESSION, status);
			if (status != OCI_SUCCESS) {
				oci_error(sth, imp_sth->errhp, status, "OCILobCreateTemporary");
				return 0;
			}

			if( ! phs->csid ) {
				ub1 csform = SQLCS_IMPLICIT;
				ub2 csid = 0;
				OCILobCharSetForm_log_stat(imp_sth,
                                           imp_sth->envhp,
                                           imp_sth->errhp,
                                           (OCILobLocator*)phs->desc_h,
                                           &csform,
                                           status );
				if (status != OCI_SUCCESS)
					return oci_error(sth, imp_sth->errhp, status, "OCILobCharSetForm");
#ifdef OCI_ATTR_CHARSET_ID
			/* Effectively only used so AL32UTF8 works properly */
				OCILobCharSetId_log_stat(imp_sth,
                                         imp_sth->envhp,
                                         imp_sth->errhp,
                                         (OCILobLocator*)phs->desc_h,
                                         &csid,
                                         status );
				if (status != OCI_SUCCESS)
					return oci_error(sth, imp_sth->errhp, status, "OCILobCharSetId");
#endif /* OCI_ATTR_CHARSET_ID */
		/* if data is utf8 but charset isn't then switch to utf8 csid */
				csid = (SvUTF8(phs->sv) && !CS_IS_UTF8(csid)) ? utf8_csid : CSFORM_IMPLIED_CSID(imp_dbh, csform);
				phs->csid = csid;
				phs->csform = csform;
			}

			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	  calling OCILobWrite phs->csid=%d phs->csform=%d amtp=%d\n",
					phs->csid, phs->csform, amtp );

		/* write lob data */

			OCILobWrite_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp,
				(OCILobLocator*)phs->desc_h, &amtp, 1, SvPVX(phs->sv), amtp, OCI_ONE_PIECE,
					0,0, phs->csid, phs->csform, status);
			if (status != OCI_SUCCESS) {
				return oci_error(sth, imp_sth->errhp, status, "OCILobWrite in dbd_rebind_ph_lob");
			}
		}
	}
	return 1;
}


#ifdef UTF8_SUPPORT
ub4
ora_blob_read_mb_piece(SV *sth, imp_sth_t *imp_sth, imp_fbh_t *fbh,
  SV *dest_sv, long offset, ub4 len, long destoffset)
{
	dTHX;
	D_imp_dbh_from_sth;
	ub4 loblen = 0;
	ub4 buflen;
	ub4 amtp = 0;
	ub4 byte_destoffset = 0;
	OCILobLocator *lobl = (OCILobLocator*)fbh->desc_h;
	sword ftype = fbh->ftype;
	sword status;

	/*
	 * We assume our caller has already done the
	 * equivalent of the following:
	 *		(void)SvUPGRADE(dest_sv, SVt_PV);
	 */
	ub1 csform = SQLCS_IMPLICIT;

	OCILobCharSetForm_log_stat(imp_sth,
                               imp_sth->envhp,
                               imp_sth->errhp,
                               lobl,
                               &csform,
                               status );
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobCharSetForm");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}
	if (ftype != ORA_CLOB) {
		oci_error(sth, imp_sth->errhp, OCI_ERROR,
			"blob_read not currently supported for non-CLOB types with OCI 8 "
			"(but with OCI 8 you can set $dbh->{LongReadLen} to the length you need,"
		"so you don't need to call blob_read at all)");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}

	OCILobGetLength_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp,
				 lobl, &loblen, status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobGetLength ora_blob_read_mb_piece");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}

	loblen -= offset;	/* only count from offset onwards */
	amtp = (loblen > len) ? len : loblen;
	buflen = 4 * amtp;

	byte_destoffset = ora_utf8_to_bytes((ub1 *)(SvPVX(dest_sv)),
					(ub4)destoffset, SvCUR(dest_sv));

	if (loblen > 0) {
		ub1 *dest_bufp;
		ub1 *buffer;

		New(42, buffer, buflen, ub1);

		OCILobRead_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobl,
				&amtp, (ub4)1 + offset, buffer, buflen,
				0, 0, (ub2)0 ,csform ,status );
			  /* lab  0, 0, (ub2)0, (ub1)SQLCS_IMPLICIT, status); */

		if (dbis->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "		OCILobRead field %d %s: LOBlen %lu, LongReadLen %lu, "
                "BufLen %lu, Got %lu\n",
				fbh->field_num+1, oci_status_name(status), ul_t(loblen),
				ul_t(imp_sth->long_readlen), ul_t(buflen), ul_t(amtp));
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobRead");
			sv_set_undef(dest_sv);	/* signal error */
			return 0;
		}

		amtp = ora_utf8_to_bytes(buffer, len, amtp);
		SvGROW(dest_sv, byte_destoffset + amtp + 1);
		dest_bufp = (ub1 *)(SvPVX(dest_sv));
		dest_bufp += byte_destoffset;
		memcpy(dest_bufp, buffer, amtp);
		Safefree(buffer);
	}
	else {
		assert(amtp == 0);
		SvGROW(dest_sv, byte_destoffset + 1);
		if (dbis->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
				"		OCILobRead field %d %s: LOBlen %lu, LongReadLen %lu, "
                "BufLen %lu, Got %lu\n",
                fbh->field_num+1, "SKIPPED", (unsigned long)loblen,
                (unsigned long)imp_sth->long_readlen, (unsigned long)buflen,
                (unsigned long)amtp);
	}

	if (dbis->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	blob_read field %d, ftype %d, offset %ld, len %lu, "
            "destoffset %ld, retlen %lu\n",
			fbh->field_num+1, ftype, offset, (unsigned long) len,
                        destoffset, ul_t(amtp));

	SvCUR_set(dest_sv, byte_destoffset+amtp);
	*SvEND(dest_sv) = '\0'; /* consistent with perl sv_setpvn etc	*/
	SvPOK_on(dest_sv);
	if (ftype == ORA_CLOB && CSFORM_IMPLIES_UTF8(imp_dbh, csform))
		SvUTF8_on(dest_sv);

	return 1;
}
#endif /* ifdef UTF8_SUPPORT */

ub4
ora_blob_read_piece(SV *sth, imp_sth_t *imp_sth, imp_fbh_t *fbh, SV *dest_sv,
			long offset, UV len, long destoffset)
{
	dTHX;
	D_imp_dbh_from_sth;
	ub4 loblen	= 0;
	ub4 buflen;
	ub4 amtp 	= 0;
	ub1 csform	= 0;
	OCILobLocator *lobl = (OCILobLocator*)fbh->desc_h;
	sword ftype	= fbh->ftype;
	sword status;
	char *type_name;

	if (ftype == ORA_CLOB)
		type_name = "CLOB";
	else if (ftype == ORA_BLOB)
		type_name = "BLOB";
	else if (ftype == ORA_BFILE)
		type_name = "BFILE";
	else {
		oci_error(sth, imp_sth->errhp, OCI_ERROR,
			"blob_read not currently supported for non-LOB types with OCI 8 "
			"(but with OCI 8 you can set $dbh->{LongReadLen} to the length you need,"
			"so you don't need to call blob_read at all)");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}

	OCILobGetLength_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobl, &loblen, status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobGetLength ora_blob_read_piece");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}

	OCILobCharSetForm_log_stat(imp_sth,
                               imp_sth->envhp,
                               imp_sth->errhp,
                               lobl,
                               &csform,
                               status );
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobCharSetForm");
		sv_set_undef(dest_sv);	/* signal error */
		return 0;
	}
	if (ftype == ORA_CLOB && csform == SQLCS_NCHAR)
		type_name = "NCLOB";

	/*
	 * We assume our caller has already done the
	 * equivalent of the following:
	 *		(void)SvUPGRADE(dest_sv, SVt_PV);
	 *		SvGROW(dest_sv, buflen+destoffset+1);
	 */

	/*	amtp is:	  LOB/BFILE  CLOB/NCLOB
	Input		 bytes	  characters
	Output FW	 bytes	  characters	(FW=Fixed Width charset, VW=Variable)
	Output VW	 bytes	  characters(in), bytes returned (afterwards)
	*/

	amtp = (loblen > len) ? len : loblen;

	/* buflen: length of buffer in bytes */
	/* so for CLOBs that'll be returned as UTF8 we need more bytes that chars */
	/* XXX the x4 here isn't perfect - really the code should be changed to loop */

	if (ftype == ORA_CLOB && CSFORM_IMPLIES_UTF8(imp_dbh, csform)) {
		buflen = amtp * 4;
	/* XXX destoffset would be counting chars here as well */
		SvGROW(dest_sv, (destoffset*4) + buflen + 1);
		if (destoffset) {
			oci_error(sth, imp_sth->errhp, OCI_ERROR,
			"blob_read with non-zero destoffset not currently supported for UTF8 values");
			sv_set_undef(dest_sv);	/* signal error */
			return 0;
		}
	}
	else {
		buflen = amtp;
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "		blob_read field %d: ftype %d %s, offset %ld, len %lu."
            "LOB csform %d, len %lu, amtp %lu, (destoffset=%ld)\n",
			fbh->field_num+1, ftype, type_name, offset, ul_t(len),
			csform,(unsigned long) (loblen), ul_t(amtp), destoffset);

	if (loblen > 0) {
		ub1 * bufp = (ub1 *)(SvPVX(dest_sv));
		bufp += destoffset;

		OCILobRead_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobl,
			&amtp, (ub4)1 + offset, bufp, buflen,
			0, 0, (ub2)0 , csform, status);

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "		OCILobRead field %d %s: LOBlen %lu, LongReadLen %lu,"
                "BufLen %lu, amtp %lu\n",
				fbh->field_num+1, oci_status_name(status), ul_t(loblen),
				ul_t(imp_sth->long_readlen), ul_t(buflen), ul_t(amtp));
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobRead");
			sv_set_undef(dest_sv);	/* signal error */
			return 0;
		}
		if (ftype == ORA_CLOB && CSFORM_IMPLIES_UTF8(imp_dbh, csform))
			SvUTF8_on(dest_sv);
	}
	else {
		assert(amtp == 0);
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
				"		OCILobRead field %d %s: LOBlen %lu, LongReadLen %lu, "
                "BufLen %lu, Got %lu\n",
				fbh->field_num+1, "SKIPPED", ul_t(loblen),
				ul_t(imp_sth->long_readlen), ul_t(buflen), ul_t(amtp));
	}

	/*
	 * We assume our caller will perform
	 * the equivalent of the following:
	 *		SvCUR(dest_sv) = amtp;
	 *		*SvEND(dest_sv) = '\0';
	 *		SvPOK_on(dest_sv);
	 */

	return(amtp);
}



static int
fetch_lob(SV *sth, imp_sth_t *imp_sth, OCILobLocator* lobloc, int ftype, SV *dest_sv, char *name)
{
	dTHX;
	D_imp_dbh_from_sth;
	ub4 loblen	= 0;
	ub4 buflen	= 0;
	ub4 amtp 	= 0;
	sword status;


	if (!name)
		name = "an unknown field";

	/* this function is not called for NULL lobs */

	/* The length is expressed in terms of bytes for BLOBs and BFILEs,	*/
	/* and in terms of characters for CLOBs	and NCLOBS			*/
	OCILobGetLength_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobloc, &loblen, status);
	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobGetLength fetch_lob");
		return 0;
	}

	if (loblen > imp_sth->long_readlen) {	/* LOB will be truncated */
		int oraperl = DBIc_COMPAT(imp_sth);
		D_imp_dbh_from_sth ;
		D_imp_drh_from_dbh ;

		/* move setting amtp up to ensure error message OK */
		amtp = imp_sth->long_readlen;
		if (DBIc_has(imp_sth,DBIcf_LongTruncOk) || (oraperl && SvIV(imp_drh -> ora_trunc))) {
			/* user says truncation is ok */
			/* Oraperl recorded the truncation in ora_errno so we	*/
			/* so also but only for Oraperl mode handles.		*/
			if (oraperl) sv_setiv(DBIc_ERR(imp_sth), 1406);
		}
		else {
			char buf[300];
			sprintf(buf,"fetching %s. LOB value truncated from %ld to %ld. %s",
				name, ul_t(loblen), ul_t(amtp),
				"DBI attribute LongReadLen too small and/or LongTruncOk not set");
			oci_error_err(sth, NULL, OCI_ERROR, buf, 24345); /* appropriate ORA error number */
			sv_set_undef(dest_sv);
			return 0;
		}
	}
	else
		amtp = loblen;

	(void)SvUPGRADE(dest_sv, SVt_PV);

	/* XXXX I've hacked on this and left it probably broken
	because I didn't have time to research which args to OCI funcs need
	to be in char or byte units. That still needs to be done.
	better variable names may help.
	(The old version (1.15) duplicated too much code here because
	I applied a contributed patch that wasn't ideal, I had too little time
	to sort it out.)
	Whatever is done here, similar changes are probably needed for the
	ora_lob_*() methods when handling CLOBs.
	*/

	/* Yep you did bust it good and bad.  Seem that when the charset of
	the client and the DB are comptiable the buflen and amtp are both in chars
	no matter how many bytes make up the chars. If it is the case were the Client's
	NLS_LANG or NLS_NCHAR is not a subset of the Server's the server will try to traslate
	the data to the Client's wishes and that is wen it uses will send the ampt value will be in bytes*/

    buflen = amtp;
    if (ftype == ORA_CLOB)
		buflen = buflen*ora_ncs_buff_mtpl;


	SvGROW(dest_sv, buflen+1);

	if (loblen > 0) {
		ub1  csform = 0;
		OCILobCharSetForm_log_stat(imp_sth,
                                   imp_sth->envhp,
                                   imp_sth->errhp,
                                   lobloc,
                                   &csform,
                                   status );
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobCharSetForm");
			sv_set_undef(dest_sv);
			return 0;
		}

	if (ftype == ORA_BFILE) {
		OCILobFileOpen_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobloc,
				(ub1)OCI_FILE_READONLY, status);
		if (status != OCI_SUCCESS) {
			oci_error(sth, imp_sth->errhp, status, "OCILobFileOpen");
			sv_set_undef(dest_sv);
			return 0;
		}
	}

	OCILobRead_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, lobloc,
		&amtp, (ub4)1, SvPVX(dest_sv), buflen,
		0, 0, (ub2)0, csform, status);

	if (status != OCI_SUCCESS ) {

		if (status == OCI_NEED_DATA ){
			char buf[300];
			sprintf(buf,"fetching %s. LOB and the read bufer is only  %lubytes, and the ora_ncs_buff_mtpl is %d, which is too small. Try setting ora_ncs_buff_mtpl to %d",
				name,  (unsigned long)buflen, ora_ncs_buff_mtpl,ora_ncs_buff_mtpl+1);

			oci_error_err(sth, NULL, OCI_ERROR, buf, OCI_NEED_DATA); /* appropriate ORA error number */
			/*croak("DBD::Oracle has returned a %s status when doing a LobRead!! \n",oci_status_name(status));*/

		/*why a croak here well if it goes on it will result in a
		  	ORA-03127: no new operations allowed until the active operation ends
		  This will result in a crash if there are any other fetchst*/
		}
		else {
			oci_error(sth, imp_sth->errhp, status, "OCILobRead");
				sv_set_undef(dest_sv);

		}
		return 0;
	}



	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 || oci_warn){
		char buf[11];
		strcpy(buf,"bytes");
		if (ftype == ORA_CLOB)
			strcpy(buf,"characters");

		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "		OCILobRead %s %s: csform %d (%s), LOBlen %lu(%s), "
            "LongReadLen %lu(%s), BufLen %lu(%s), Got %lu(%s)\n",
            name, oci_status_name(status), csform, oci_csform_name(csform),
            ul_t(loblen),buf ,
            ul_t(imp_sth->long_readlen),buf, ul_t(buflen),buf, ul_t(amtp),buf);

    }
	if (ftype == ORA_BFILE) {
		OCILobFileClose_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp,
		lobloc, status);
	}

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCILobFileClose");
		sv_set_undef(dest_sv);
		return 0;
	}

	/* tell perl what we've put in its dest_sv */
	SvCUR(dest_sv) = amtp;
	*SvEND(dest_sv) = '\0';
        if (ftype == ORA_CLOB && CSFORM_IMPLIES_UTF8(imp_dbh, csform)) /* Don't set UTF8 on BLOBs */
 		SvUTF8_on(dest_sv);
        ora_free_templob(sth, imp_sth, lobloc);
	}
	else {			/* LOB length is 0 */
		assert(amtp == 0);
		/* tell perl what we've put in its dest_sv */
		SvCUR(dest_sv) = amtp;
		*SvEND(dest_sv) = '\0';
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "		OCILobRead %s %s: LOBlen %lu, LongReadLen %lu, "
                "BufLen %lu, Got %lu\n",
				name, "SKIPPED", ul_t(loblen),
 				ul_t(imp_sth->long_readlen), ul_t(buflen), ul_t(amtp));
	}

	SvPOK_on(dest_sv);

	return 1;
}

static int
fetch_func_autolob(SV *sth, imp_fbh_t *fbh, SV *dest_sv)
{
	dTHX;
	char name[64];
	sprintf(name, "field %d of %d", fbh->field_num, DBIc_NUM_FIELDS(fbh->imp_sth));
	return fetch_lob(sth, fbh->imp_sth, (OCILobLocator*)fbh->desc_h, fbh->ftype, dest_sv, name);
}


static int
fetch_func_getrefpv(SV *sth, imp_fbh_t *fbh, SV *dest_sv)
{
	dTHX;
	if( sth ) { /* For GCC not to warn on unused parameter */ }
	/* See the Oracle::OCI module for how to actually use this! */
	sv_setref_pv(dest_sv, fbh->bless, (void*)fbh->desc_h);
	return 1;
}

#ifdef OCI_DTYPE_REF
static void
fbh_setup_getrefpv(imp_sth_t *imp_sth, imp_fbh_t *fbh, int desc_t, char *bless)
{
	dTHX;
	if (DBIc_DBISTATE(imp_sth)->debug >= 2 || dbd_verbose >= 3 )
        PerlIO_printf(DBIc_LOGPIO(imp_sth),
		"	col %d: otype %d, desctype %d, %s", fbh->field_num, fbh->dbtype, desc_t, bless);
	fbh->ftype  = fbh->dbtype;
	fbh->disize = fbh->dbsize;
	fbh->fetch_func = fetch_func_getrefpv;
	fbh->bless  = bless;
	fbh->desc_t = desc_t;
	OCIDescriptorAlloc_ok(imp_sth, fbh->imp_sth->envhp, &fbh->desc_h, fbh->desc_t);
}
#endif


static int
calc_cache_rows(int cache_rows, int num_fields, int est_width, int has_longs,ub4 prefetch_memory)
{
	dTHX;
	/* Use guessed average on-the-wire row width calculated above &	*/
	/* add in overhead of 5 bytes per field plus 8 bytes per row.	*/
	/* The n*5+8 was determined by studying SQL*Net v2 packets.	*/
	/* It could probably benefit from a more detailed analysis.	*/

	est_width += num_fields*5 + 8;

	if (has_longs) {			/* override/disable caching	*/
		cache_rows = 1;		/* else read_blob can't work	*/
	}
	else if (prefetch_memory) { /*set rows by memory*/

		cache_rows=prefetch_memory/est_width;
	}
	else{
		if (cache_rows == 0) {		/* automatically size the cache	*/
		/* automatically size the cache	*/

		/* Oracle packets on ethernet have max size of around 1460.	*/
		/* We'll aim to fill our row cache with around 10 per go.	*/
		/* Using 10 means any 'runt' packets will have less impact.	*/
		/* orginally set up as above but playing around with newer versions*/
		/* I found that 500 was much faster*/
		int txfr_size  = 10 * 1460;	/* desired transfer/cache size	*/

		cache_rows = txfr_size / est_width;		  /* (maybe 1 or 0)	*/

		/* To ensure good performance with large rows (near or larger	*/
		/* than our target transfer size) we set a minimum cache size.	*/
		/* I made them all at least 10* what they were before this */
		/* main reasoning this old value reprewneted a norm in the oralce 7~8 */
		/* 9 to 11 can handel much much more */
		if (cache_rows < 60)	/* is cache a 'useful' size?	*/
			cache_rows = (cache_rows > 0) ? 60 : 40;
		}
	}
	if (cache_rows > 10000000)	/* keep within Oracle's limits  */
		cache_rows = 10000000;	/* seems it was ub2 at one time now ub4 this number is arbitary on my part*/


	return cache_rows;
}

/* called by get_object to return the actual value in the property */

static void get_attr_val(SV *sth,AV *list,imp_fbh_t *fbh, text  *name , OCITypeCode  typecode, dvoid	*attr_value )
{
	dTHX;
    D_imp_sth(sth);
	text		str_buf[200];
	double		dnum;
	size_t		str_len;
	ub4			ub4_str_len;
	OCIRaw		*raw 	= (OCIRaw *) 0;
	OCIString	*vs 	= (OCIString *) 0;
	ub1			*temp	= (ub1 *)0;
	ub4			rawsize = 0;
	ub4			i 		= 0;
	sword		status;
	SV			*raw_sv;

  /* get the data based on the type code*/
	if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ) {
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
                      " getting value of object attribute named  %s with typecode=%s\n",
                      name,oci_typecode_name(typecode));
	}

	switch (typecode)
	{

	case OCI_TYPECODE_INTERVAL_YM  :
	case OCI_TYPECODE_INTERVAL_DS  :

      OCIIntervalToText_log_stat(fbh->imp_sth,
                                 fbh->imp_sth->envhp,
                                 fbh->imp_sth->errhp,
                                 attr_value,
                                 str_buf,
                                 (size_t) 200,
                                 &str_len,
                                 status);
		str_buf[str_len+1] = '\0';
		av_push(list, newSVpv( (char *) str_buf,0));
		break;

	case OCI_TYPECODE_TIMESTAMP_TZ :
	case OCI_TYPECODE_TIMESTAMP_LTZ :
	case OCI_TYPECODE_TIMESTAMP :


		ub4_str_len = 200;
		OCIDateTimeToText_log_stat(fbh->imp_sth,
                                   fbh->imp_sth->envhp,
                                   fbh->imp_sth->errhp,
                                   attr_value,
                                   &ub4_str_len,
                                   str_buf,
                                   status);

		if (typecode == OCI_TYPECODE_TIMESTAMP_TZ || typecode == OCI_TYPECODE_TIMESTAMP_LTZ){
			char s_tz_hour[6]="000";
			char s_tz_min[6]="000";
			sb1 tz_hour;
			sb1 tz_minute;
			status = OCIDateTimeGetTimeZoneOffset (fbh->imp_sth->envhp,
												 fbh->imp_sth->errhp,
												 *(OCIDateTime**)attr_value,
												 &tz_hour,
									&tz_minute );

			if (  (tz_hour<0) && (tz_hour>-10) ){
				sprintf(s_tz_hour," %03d",tz_hour);
			} else {
				sprintf(s_tz_hour," %02d",tz_hour);
			}

			sprintf(s_tz_min,":%02d", tz_minute);
			strcat((signed char*)str_buf, s_tz_hour);
			strcat((signed char*)str_buf, s_tz_min);
			str_buf[ub4_str_len+7] = '\0';

		} else {
		  str_buf[ub4_str_len+1] = '\0';
		}

		av_push(list, newSVpv( (char *) str_buf,0));
		break;

	case OCI_TYPECODE_DATE :						 /* fixed length string*/
		ub4_str_len = 200;
		OCIDateToText_log_stat(fbh->imp_sth,
                               fbh->imp_sth->errhp,
                               (CONST OCIDate *) attr_value,
                               &ub4_str_len,
                               str_buf,
                               status);
		str_buf[ub4_str_len+1] = '\0';
		av_push(list, newSVpv( (char *) str_buf,0));
		break;


	case OCI_TYPECODE_CLOB:
	case OCI_TYPECODE_BLOB:
	case OCI_TYPECODE_BFILE:
		raw_sv = newSV(0);
		fetch_lob(sth, fbh->imp_sth,*(OCILobLocator**)attr_value, typecode, raw_sv, (signed char*)name);


		av_push(list, raw_sv);
		break;

	case OCI_TYPECODE_RAW :/* RAW*/

		raw_sv = newSV(0);
		raw = *(OCIRaw **) attr_value;
		temp = OCIRawPtr(fbh->imp_sth->envhp, raw);
		rawsize = OCIRawSize (fbh->imp_sth->envhp, raw);
		for (i=0; i < rawsize; i++) {
			sv_catpvf(raw_sv,"0x%x ", temp[i]);
		}
		sv_catpv(raw_sv,"\n");

		av_push(list, raw_sv);

		 break;
	case OCI_TYPECODE_CHAR :						 /* fixed length string */
	case OCI_TYPECODE_VARCHAR :								 /* varchar  */
	case OCI_TYPECODE_VARCHAR2 :								/* varchar2 */
		vs = *(OCIString **) attr_value;
		av_push(list, newSVpv((char *) OCIStringPtr(fbh->imp_sth->envhp, vs),0));
		break;
	case OCI_TYPECODE_SIGNED8 :							  /* BYTE - sb1  */
		av_push(list, newSVuv(*(sb1 *)attr_value));
		break;
	case OCI_TYPECODE_UNSIGNED8 :					/* UNSIGNED BYTE - ub1  */
		av_push(list, newSViv(*(ub1 *)attr_value));
		break;
	case OCI_TYPECODE_OCTET :										/* OCT*/
		av_push(list, newSViv(*(ub1 *)attr_value));
		break;
	case OCI_TYPECODE_UNSIGNED16 :						/* UNSIGNED SHORT  */
	case OCI_TYPECODE_UNSIGNED32 :						/* UNSIGNED LONG  */
	case OCI_TYPECODE_REAL :									 /* REAL	*/
	case OCI_TYPECODE_DOUBLE :									/* DOUBLE  */
	case OCI_TYPECODE_INTEGER :									 /* INT  */
	case OCI_TYPECODE_SIGNED16 :								  /* SHORT  */
	case OCI_TYPECODE_SIGNED32 :									/* LONG  */
	case OCI_TYPECODE_DECIMAL :								 /* DECIMAL  */
	case OCI_TYPECODE_FLOAT :									/* FLOAT	*/
	case OCI_TYPECODE_NUMBER :								  /* NUMBER	*/
	case OCI_TYPECODE_SMALLINT :								/* SMALLINT */
		(void) OCINumberToReal(fbh->imp_sth->errhp, (CONST OCINumber *) attr_value,
								(uword) sizeof(dnum), (dvoid *) &dnum);

		av_push(list, newSVnv(dnum));
		break;
	default:
		break;
	}
}


SV* new_ora_object (AV* list, OCITypeCode typecode) {
	dTHX;
	SV* objref = newRV_noinc((SV*) list);

	if (ora_objects && typecode == OCI_TYPECODE_OBJECT) {
		HV* self = newHV();
		(void)hv_store(self, "type_name", 9, av_shift(list), 0);
		(void)hv_store(self, "attributes", 10, objref, 0);
		objref = newRV_noinc((SV*) self);
		objref = sv_bless(objref, gv_stashpv("DBD::Oracle::Object", 0));

	}
	return objref;
}

/*gets the properties of an object from a fetch by using the attributes saved in the describe */

int
get_object (SV *sth, AV *list, imp_fbh_t *fbh,fbh_obj_t *base_obj,OCIComplexObject *value, OCIType *instance_tdo, dvoid *obj_ind){

	dTHX;
    D_imp_sth(sth);
	sword 		status;
	dvoid		*element ;
	dvoid		*attr_value;
	boolean		eoc;
	ub2	 		pos;
	dvoid 		*attr_null_struct;
	OCIInd		attr_null_status;
	OCIInd		*element_null;
	OCIType 	*attr_tdo;
	OCIIter		*itr;
	fbh_obj_t	*fld;
	fbh_obj_t	*obj = base_obj;

	 OCIType	*tdo = instance_tdo ? instance_tdo : obj->tdo;

     if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ) {
         PerlIO_printf(DBIc_LOGPIO(imp_sth),
                       " getting attributes of object named  %s with typecode=%s\n",
                       obj->type_name,oci_typecode_name(obj->typecode));
	}

	switch (obj->typecode) {

		case OCI_TYPECODE_OBJECT:	/* embedded ADT */
		case OCI_TYPECODE_OPAQUE: /*doesn't do anything though*/
			if (ora_objects){


				sword	status;
				if (!instance_tdo && !obj->is_final_type) {
					OCIRef	*type_ref=0;
					status = OCIObjectNew(fbh->imp_sth->envhp, fbh->imp_sth->errhp, fbh->imp_sth->svchp,
											OCI_TYPECODE_REF, (OCIType *)0,
											(dvoid *)0, OCI_DURATION_DEFAULT, TRUE,
											(dvoid **) &type_ref);
					if (status != OCI_SUCCESS) {
						oci_error(sth, fbh->imp_sth->errhp, status, "OCIObjectNew");
						return 0;
					}

					status=OCIObjectGetTypeRef(fbh->imp_sth->envhp,fbh->imp_sth->errhp, (dvoid*)value, type_ref);
					if (status != OCI_SUCCESS) {
						oci_error(sth, fbh->imp_sth->errhp, status, "OCIObjectGetTypeRef");
						return 0;
					}

					OCITypeByRef_log_stat(fbh->imp_sth,
                                          fbh->imp_sth->envhp,
                                          fbh->imp_sth->errhp,
                                          type_ref,
                                          &tdo,status);

					if (status != OCI_SUCCESS) {
						oci_error(sth, fbh->imp_sth->errhp, status, "OCITypeByRef");
						return 0;
					}

					status = OCIObjectFree(fbh->imp_sth->envhp, fbh->imp_sth->errhp, type_ref, (ub2)0);

					if (status != OCI_SUCCESS) {
						oci_error(sth, fbh->imp_sth->errhp, status, "OCIObjectFree");
						return 0;
					}

				}


				if (tdo != obj->tdo) {
					/* this is subtype -> search for subtype obj */
					while (obj->next_subtype && tdo != obj->tdo) {
						obj = obj->next_subtype;
					}
					if (tdo != obj->tdo) {
						/* new subtyped -> get obj description */
						if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ) {
							PerlIO_printf(DBIc_LOGPIO(imp_sth), " describe subtype (tdo=%p) of object type %s (tdo=%p)\n",(void*)tdo,base_obj->type_name,(void*)base_obj->tdo);
						}

						Newz(1, obj->next_subtype, 1, fbh_obj_t);
						obj->next_subtype->tdo = tdo;
						if ( describe_obj_by_tdo(sth, fbh->imp_sth, obj->next_subtype, 0 /*unknown level there*/) ) {
							obj = obj->next_subtype;
							if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ){
								dump_struct(fbh->imp_sth,obj,0);
							}
						}
						else {
							obj->next_subtype = 0;
						}
					}

					if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ) {
						PerlIO_printf(DBIc_LOGPIO(imp_sth), " getting attributes of object subtype  %s\n",obj->type_name);
					}
				}

				av_push(list, newSVpv((char*)obj->type_name, obj->type_namel));
			}



			for (pos = 0; pos < obj->field_count; pos++){

				fld = &obj->fields[pos]; /*get the field */

				if (ora_objects) {
					/* add field name */
					av_push(list, newSVpv((char*)fld->type_name, fld->type_namel));
				}

/*
the little bastard above took me ages to find out
seems Oracle does not like people to know that it can do this
the concept is simple really
 1. pin the object
 2. bind with dty = SQLT_NTY
 3. OCIDefineObject using the TDO
 4. one gets the null indicator of the objcet with OCIObjectGetInd
	The the obj_ind is for the entier object not the properties so you call it once it
	gets all of the indicators for the objects so you pass it into OCIObjectGetAttr and that
	function will set attr_null_status as in the get below.
 5. interate over the attributes of the object

The thing to remember is that OCI and C have no way of representing a DB NULLs so we use the OCIInd find out
if the object or any of its properties are NULL, This is one little line in a 20 chapter book and even then
id only shows you examples with the C struct built in and only a single record. Nowhere does it say you can do it this way.
*/

				OCIObjectGetAttr_log_stat(
                    fbh->imp_sth,
                    fbh->imp_sth->envhp,
                    fbh->imp_sth->errhp,
                    value,                      /* instance */
                    obj_ind,                    /* null_struct */
                    tdo,                        /* tdo */
                    (CONST oratext**)&fld->type_name, /* names */
                    &fld->type_namel,                 /* lengths */
                    1,                                /* name_count */
                    (ub4 *)0,                         /* indexes */
                    0,                                /* index_count */
                    &attr_null_status,                /* attr_null_status */
                    &attr_null_struct,                /* attr_null_struct */
                    &attr_value,                      /* attr_value */
                    &attr_tdo,                        /* attr_tdo */
                    status);

				if (status != OCI_SUCCESS) {
					oci_error(sth, fbh->imp_sth->errhp, status, "OCIObjectGetAttr");
					return 0;
				}

				if (attr_null_status==OCI_IND_NULL){
					 av_push(list,  &PL_sv_undef);
				} else {
					if (fld->typecode == OCI_TYPECODE_OBJECT || fld->typecode == OCI_TYPECODE_VARRAY || fld->typecode == OCI_TYPECODE_TABLE || fld->typecode == OCI_TYPECODE_NAMEDCOLLECTION){

						fld->fields[0].value = newAV();
						if (fld->typecode != OCI_TYPECODE_OBJECT)
							attr_value = *(dvoid **)attr_value;

						if (!get_object (sth,fld->fields[0].value, fbh, &fld->fields[0],attr_value, attr_tdo, attr_null_struct))
							return 0;
						av_push(list, new_ora_object(fld->fields[0].value, fld->typecode));

					} else{  /* else, display the scaler type attribute */

						get_attr_val(sth,list, fbh, fld->type_name, fld->typecode, attr_value);

					}
				}
			 }
			break;

		case OCI_TYPECODE_REF :								/* embedded ADT */
			croak("panic: OCI_TYPECODE_REF objets () are not supported ");
			break;

		case OCI_TYPECODE_NAMEDCOLLECTION : /*this works for both as I am using CONST OCIColl */

			switch (obj->col_typecode) { /*there may be more thatn two I havn't found them yet mmight be XML??*/
				case OCI_TYPECODE_TABLE :					/* nested table */
				case OCI_TYPECODE_VARRAY :					/* variable array */
					fld = &obj->fields[0]; /*get the field */
					OCIIterCreate_log_stat(fbh->imp_sth,
                                           fbh->imp_sth->envhp,
                                           fbh->imp_sth->errhp,
                                           (OCIColl*) value,
                                           &itr,
                                           status);
					if (status != OCI_SUCCESS) {
						/*not really an error just no data
						oci_error(sth, fbh->imp_sth->errhp, status, "OCIIterCreate");*/
						status = OCI_SUCCESS;
						av_push(list,  &PL_sv_undef);
						return 0;
					}
					for(eoc = FALSE;!OCIIterNext(fbh->imp_sth->envhp, fbh->imp_sth->errhp, itr,
						(dvoid **) &element,
						(dvoid **) &element_null, &eoc) && !eoc;)
					{

						if (*element_null==OCI_IND_NULL){
							av_push(list,  &PL_sv_undef);
						} else {
							if (obj->element_typecode == OCI_TYPECODE_OBJECT || obj->element_typecode == OCI_TYPECODE_VARRAY || obj->element_typecode== OCI_TYPECODE_TABLE || obj->element_typecode== OCI_TYPECODE_NAMEDCOLLECTION){
								fld->value = newAV();
								if(!get_object (sth,fld->value, fbh, fld,element,0,element_null))
									return 0;
								av_push(list, new_ora_object(fld->value, obj->element_typecode));
							} else{  /* else, display the scaler type attribute */
								get_attr_val(sth,list, fbh, obj->type_name, obj->element_typecode, element);
							}
						}
					}
					/*nasty surprise here. one has to get rid of the iterator or you will leak memory
					  not documented in oci or in demos */
					OCIIterDelete_log_stat(fbh->imp_sth,
                                           fbh->imp_sth->envhp,
                                           fbh->imp_sth->errhp,
                                           &itr,
                                           status );
					if (status != OCI_SUCCESS) {
						oci_error(sth, fbh->imp_sth->errhp, status, "OCIIterDelete");
						return 0;
					}
					break;
				default:
					break;
				}
			break;
		default:
			if (value) {
				get_attr_val(sth,list, fbh, obj->type_name, obj->typecode, value);
			}
			else
				return 1;
			break;
		}
		return 1;
 }



/*cutsom fetch for embedded objects */

static int
fetch_func_oci_object(SV *sth, imp_fbh_t *fbh,SV *dest_sv)
{
	dTHX;
        D_imp_sth(sth);

	if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 ) {
		PerlIO_printf(DBIc_LOGPIO(imp_sth),
                      " getting an embedded object named  %s with typecode=%s\n",
                      fbh->obj->type_name,oci_typecode_name(fbh->obj->typecode));
	}

	if (fbh->obj->obj_ind && fbh->obj->obj_ind[0] == OCI_IND_NULL) {
		sv_set_undef(dest_sv);
		return 1;
	}

	fbh->obj->value=newAV();

	/*will return referance to an array of scalars*/
	if (!get_object(sth,fbh->obj->value,fbh,fbh->obj,fbh->obj->obj_value,0,fbh->obj->obj_ind)){
 		return 0;
	} else {
		sv_setsv(dest_sv, sv_2mortal(new_ora_object(fbh->obj->value, fbh->obj->typecode)));
		return 1;
	}

}



static int
fetch_clbk_lob(SV *sth, imp_fbh_t *fbh,SV *dest_sv){

	dTHX;
	D_imp_sth(sth);
	D_imp_dbh_from_sth;
	fb_ary_t *fb_ary = fbh->fb_ary;

	ub4 actual_bufl=imp_sth->piece_size*(fb_ary->piece_count)+fb_ary->bufl;

	if (fb_ary->piece_count==0){
		if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "  Fetch persistent lob of %d (char/bytes) with callback in 1 "
                "piece of %d (Char/Bytes)\n",
                actual_bufl,fb_ary->bufl);

		memcpy(fb_ary->cb_abuf,fb_ary->abuf,fb_ary->bufl );

	} else {
        if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "  Fetch persistent lob of %d (Char/Bytes) with callback in %d "
                "piece(s) of %d (Char/Bytes) and one piece of %d (Char/Bytes)\n",
                actual_bufl,fb_ary->piece_count,fbh->piece_size,fb_ary->bufl);

		memcpy(fb_ary->cb_abuf+imp_sth->piece_size*(fb_ary->piece_count),fb_ary->abuf,fb_ary->bufl );
	}

	if (fbh->ftype == SQLT_BIN){
		*(fb_ary->cb_abuf+(actual_bufl))='\0'; /* add a null teminator*/
		sv_setpvn(dest_sv, (char*)fb_ary->cb_abuf,(STRLEN)actual_bufl);
	} else {
		sv_setpvn(dest_sv, (char*)fb_ary->cb_abuf,(STRLEN)actual_bufl);
		if (CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) ){
			SvUTF8_on(dest_sv);
		}
	}
	return 1;
}
/* This is another way to get lobs as a alternate to callback */

static int
fetch_get_piece(SV *sth, imp_fbh_t *fbh,SV *dest_sv)
{
	dTHX;
	D_imp_sth(sth);
	D_imp_dbh_from_sth;
	fb_ary_t *fb_ary = fbh->fb_ary;
	ub4 buflen		 = fb_ary->bufl;
	ub4 actual_bufl	 = 0;
	ub1	piece  = OCI_FIRST_PIECE;
	void *hdlptr = (dvoid *) 0;
	ub4 hdltype  = OCI_HTYPE_DEFINE, iter = 0, idx = 0;
	ub1	in_out = 0;
	sb2	indptr = 0;
	ub2	rcode  = 0;
	sword status = OCI_NEED_DATA;

	if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 ) {
		PerlIO_printf(DBIc_LOGPIO(imp_sth), "in fetch_get_piece  \n");
	}

	while (status == OCI_NEED_DATA){

        OCIStmtGetPieceInfo_log_stat(fbh->imp_sth,
                                     fbh->imp_sth->stmhp,
                                     fbh->imp_sth->errhp,
                                     &hdlptr,
                                     &hdltype,
                                     &in_out,
                                     &iter,
                                     &idx,
                                     &piece,
                                     status);

		/* This is how this works
		First we get the piece Info above
		the bugger thing is that this will get the piece info in sequential order so on each call to the above
		you have to check to ensure you have the right define handle from the OCIDefineByPos
		I do it in the next if statement.  So this will loop untill the handle changes at that point it exits the loop
		during the loop I add the abuf to the  cb_abuf  using the buflen that is set above.
		I get the actual buffer length by adding up all the pieces (buflen) as I go along
		Another really anoying thing is once can only find out if there is data left over at the very end of the fetching of the colums
		so I make it warn if the LongTruncOk. I could also do this before but that would not result in any of the good data getting
		in
		*/
		if ( hdlptr==fbh->defnp){

			OCIStmtSetPieceInfo_log_stat(fbh->imp_sth,
                                         fbh->defnp,
										 fbh->imp_sth->errhp,
										 fb_ary->abuf,
										 &buflen,
										 piece,
										 (dvoid *)&indptr,
										 &rcode,status);


            OCIStmtFetch_log_stat(fbh->imp_sth, fbh->imp_sth->stmhp,fbh->imp_sth->errhp,1,(ub2)OCI_FETCH_NEXT,OCI_DEFAULT,status);


			if (status==OCI_SUCCESS_WITH_INFO && !DBIc_has(fbh->imp_sth,DBIcf_LongTruncOk)){
			 	dTHR; 			/* for DBIc_ACTIVE_off	*/
				DBIc_ACTIVE_off(fbh->imp_sth);	/* eg finish		*/
				oci_error(sth, fbh->imp_sth->errhp, status, "OCIStmtFetch, LongReadLen too small and/or LongTruncOk not set");
			}
 			memcpy(fb_ary->cb_abuf+fb_ary->piece_count*imp_sth->piece_size,fb_ary->abuf,buflen );
			fb_ary->piece_count++;/*used to tell me how many pieces I have, for debuffing in this case */
			actual_bufl=actual_bufl+buflen;

		}else {
			status=OCI_LAST_PIECE;
		}
	}


	if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 ){
		if (fb_ary->piece_count==1){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	 Fetch persistent lob of %d (Char/Bytes) with Polling "
                "in 1 piece\n",
                actual_bufl);

		} else {
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	 Fetch persistent lob of %d (Char/Bytes) with Polling "
                "in %d piece(s) of %d (Char/Bytes) and one piece of %d (Char/Bytes)\n",
                actual_bufl,fb_ary->piece_count,fbh->piece_size,buflen);
		}
	}

	if (actual_bufl > 0){
		sv_setpvn(dest_sv, (char*)fb_ary->cb_abuf,(STRLEN)actual_bufl);
		if (fbh->ftype != SQLT_BIN){

			if (CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) ){ /* do the UTF 8 magic*/
				SvUTF8_on(dest_sv);
			}
		}
	} else {
		sv_set_undef(dest_sv);
	}

	return 1;
}


int
empty_oci_object(fbh_obj_t *obj){
	dTHX;
	int			pos =0;
	fbh_obj_t	*fld=NULL;



	switch (obj->element_typecode) {

		case OCI_TYPECODE_OBJECT :		/* embedded ADT */
		case OCI_TYPECODE_OPAQUE : /*usually an XML object*/
			if (obj->next_subtype) {
				empty_oci_object(obj->next_subtype);
			}

			for (pos = 0; pos < obj->field_count; pos++){
				fld = &obj->fields[pos]; /*get the field */
				if (fld->typecode == OCI_TYPECODE_OBJECT || fld->typecode == OCI_TYPECODE_VARRAY || fld->typecode == OCI_TYPECODE_TABLE || fld->typecode == OCI_TYPECODE_NAMEDCOLLECTION){
					empty_oci_object(fld);
					if (fld->value && SvTYPE(fld->value) == SVt_PVAV){
						av_clear(fld->value);
			 			av_undef(fld->value);
					}
				}
				else {
					return 1;
				}
			}
			break;

		case OCI_TYPECODE_NAMEDCOLLECTION :
			fld = &obj->fields[0]; /*get the field */
			if (obj->element_typecode == OCI_TYPECODE_OBJECT){
				empty_oci_object(fld);
			}
			if (fld->value && SvTYPE(fld->value)){
				if (SvTYPE(fld->value) == SVt_PVAV){
					av_clear(fld->value);
					av_undef(fld->value);
				}
			}
			break;

		default:
		 	break;
	}
	if ( fld && fld->value && (SvTYPE(fld->value) == SVt_PVAV) ){
			av_clear(obj->value);
		av_undef(obj->value);
	}

	return 1;

}

static void
fetch_cleanup_pres_lobs(SV *sth,imp_fbh_t *fbh){
	dTHX;
    D_imp_sth(sth);

	fb_ary_t *fb_ary = fbh->fb_ary;

	if( sth ) { /* For GCC not to warn on unused parameter*/  }
	fb_ary->piece_count=0;/*reset the peice counter*/
	memset( fb_ary->abuf, '\0', fb_ary->bufl); /*clean out the piece fetch buffer*/
	fb_ary->bufl=fbh->piece_size; /*reset this back to the piece length */
	fb_ary->cb_bufl=fbh->disize; /*reset this back to the max size for the fetch*/
	memset( fb_ary->cb_abuf, '\0', fbh->disize ); /*clean out the call back buffer*/

 	if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 )
		PerlIO_printf(DBIc_LOGPIO(imp_sth),"  fetch_cleanup_pres_lobs \n");

	return;
}

static void
fetch_cleanup_oci_object(SV *sth, imp_fbh_t *fbh){
	dTHX;
    D_imp_sth(sth);

	if( sth ) { /* For GCC not to warn on unused parameter*/  }

	if (fbh->obj){
		if(fbh->obj->obj_value || fbh->obj->obj_ind){
			empty_oci_object(fbh->obj);
		}
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
        PerlIO_printf(DBIc_LOGPIO(imp_sth),"  fetch_cleanup_oci_object \n");
	return;
}

void rs_array_init(imp_sth_t *imp_sth)
{
	dTHX;

	imp_sth->rs_array_num_rows	=0;
	imp_sth->rs_array_idx		=0;
	imp_sth->rs_fetch_count		=0;
	imp_sth->rs_array_status	=OCI_SUCCESS;

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	rs_array_init:imp_sth->rs_array_size=%d, rs_array_idx=%d, "
            "prefetch_rows=%d, rs_array_status=%s\n",
            imp_sth->rs_array_size, imp_sth->rs_array_idx, imp_sth->prefetch_rows,
            oci_status_name(imp_sth->rs_array_status));
}

static int			/* --- Setup the row cache for this sth --- */
sth_set_row_cache(SV *h, imp_sth_t *imp_sth, int max_cache_rows, int num_fields, int has_longs)
{
	dTHX;
	D_imp_dbh_from_sth;
	D_imp_drh_from_dbh;
	int num_errors		= 0;
	ub4 prefetch_mem	= 0; /*Oracle prefetch memory buffer*/
	sb4 prefetch_rows	= 0; /*Oracle prefetch Row Buffer*/
	sb4 cache_rows		= 0;/* set high so memory is the limit */
	sword status;



	if (imp_sth->RowCacheSize ) { /*Statment value will crump the handle value */
		cache_rows=imp_sth->RowCacheSize;
	}
	else if (imp_dbh->RowCacheSize){
		cache_rows=imp_dbh->RowCacheSize;

	}

	/* seems that RowCacheSize was incorrectly used in the past
	   in the DBI Spect  RowCacheSize is to be used for a local row cache
	   and can be set on both the handle and the statement and the statement will take
	   precideace

	   From DBI POD
	      A hint to the driver indicating the size of the local
	      row cache that the application would like the driver to
	      use for future SELECT statements.

	   so RowCacheSize is for a local cache to cut down on round trips

	   The OCI doc state that both OCI_ATTR_PREFETCH_ROWS OCI_ATTR_PREFETCH_MEMORY
	   sets up a cleint side cache but in earlier version than 1.24 we only selected
	   one record at a time from the fetch this means a round trip (at least to the local cache)
	   at each fetch.

	   With the new array fetch we truly have a local cache so I will use it
	   RowCacheSize to set the value of that cache or the array fetch*/



	/* number of rows to cache	 if using oraperl  will leave this in for now*/


	if (SvOK(imp_drh->ora_cache_o)){
		imp_sth->cache_rows = SvIV(imp_drh->ora_cache_o);
	}
	else if (SvOK(imp_drh->ora_cache)){
		imp_sth->cache_rows = SvIV(imp_drh->ora_cache);
	}


	prefetch_rows	=imp_sth->prefetch_rows;
	prefetch_mem	=imp_sth->prefetch_memory;


	if (!cache_rows) { /*start with this value if not set then set default cache */

		cache_rows=calc_cache_rows(imp_sth->cache_rows,(int)num_fields, imp_sth->est_width, has_longs,0);

		if(!prefetch_rows && !prefetch_mem){ /*if there are not prefetch rows make sure I set it here to the default*/
			  prefetch_rows=cache_rows;
		}
	}
	else if (imp_dbh->RowCacheSize < 0) {/* for compaibility with DBI doc negitive value here means use the value as memory*/
		prefetch_mem	=-imp_dbh->RowCacheSize; /* cache_mem always +ve here */
		prefetch_rows	=0;
		cache_rows=calc_cache_rows(imp_sth->cache_rows,(int)num_fields, imp_sth->est_width, has_longs,prefetch_mem);
		/*The above fucntion will set the cache_rows using memory as the limit*/
	}
	else {

	   if (!prefetch_mem){
			prefetch_rows = cache_rows; /*use the RowCacheSize*/
	   }
	}

	if (cache_rows <= prefetch_rows){
		cache_rows=prefetch_rows;
		/* is prefetch_rows are greater than the RowCahceSize then use prefetch_rows*/
	}

	OCIAttrSet_log_stat(imp_sth, imp_sth->stmhp, OCI_HTYPE_STMT,
						&prefetch_mem,  sizeof(prefetch_mem), OCI_ATTR_PREFETCH_MEMORY,
						imp_sth->errhp, status);

	if (status != OCI_SUCCESS) {
		oci_error(h, imp_sth->errhp, status,
				"OCIAttrSet OCI_ATTR_PREFETCH_MEMORY");
		++num_errors;
	}

	OCIAttrSet_log_stat(imp_sth, imp_sth->stmhp, OCI_HTYPE_STMT,
					&prefetch_rows, sizeof(prefetch_rows), OCI_ATTR_PREFETCH_ROWS,
				imp_sth->errhp, status);

	if (status != OCI_SUCCESS) {
		oci_error(h, imp_sth->errhp, status, "OCIAttrSet OCI_ATTR_PREFETCH_ROWS");
		++num_errors;
	}


	imp_sth->rs_array_size=cache_rows;

    if (max_cache_rows){/* limited to 1 by a cursor or something else*/
		imp_sth->rs_array_size=1;
	}


	if (imp_sth->row_cache_off){/*set the size of the Rows in Cache value*/
		imp_dbh->RowsInCache =1;
		imp_sth->RowsInCache =1;
	}
	 else {
		imp_dbh->RowsInCache=imp_sth->rs_array_size;
		imp_sth->RowsInCache=imp_sth->rs_array_size;
	}



	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 || oci_warn) /*will also display if oci_warn is on*/
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
			"	cache settings DB Handle RowCacheSize=%d,Statement Handle "
            "RowCacheSize=%d, OCI_ATTR_PREFETCH_ROWS=%lu, "
            "OCI_ATTR_PREFETCH_MEMORY=%lu, Rows per Fetch=%d, Multiple Row Fetch=%s\n",
			imp_dbh->RowCacheSize, imp_sth->RowCacheSize,
            (unsigned long) (prefetch_rows), (unsigned long) (prefetch_mem),
            cache_rows,(imp_sth->row_cache_off)?"Off":"On");

	return num_errors;
}



/*recurses down the field's TDOs and saves the little bits it need for later use on a fetch fbh->obj */
int
describe_obj(SV *sth,imp_sth_t *imp_sth,OCIParam *parm,fbh_obj_t *obj,int level )
{
	dTHX;
	sword status;
	OCIRef *type_ref;

	if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ) {
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "At level=%d in description an embedded object \n",level);
	}
	/*Describe the field (OCIParm) we know it is a object or a collection */

	/* Get the Actual TDO */
	OCIAttrGet_parmdp(imp_sth,parm, &type_ref, 0, OCI_ATTR_REF_TDO, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCIAttrGet");
		return 0;
	}

	OCITypeByRef_log_stat(imp_sth,
                          imp_sth->envhp,
                          imp_sth->errhp,
                          type_ref,
                          &obj->tdo,
                          status);

	if (status != OCI_SUCCESS) {
		oci_error(sth, imp_sth->errhp, status, "OCITypeByRef");
		return 0;
	}

	return describe_obj_by_tdo(sth, imp_sth, obj, level);
	}

int
describe_obj_by_tdo(SV *sth,imp_sth_t *imp_sth,fbh_obj_t *obj,ub2 level ) {
	dTHX;
	sword status;
	text *type_name, *schema_name;
	ub4  type_namel, schema_namel;


	OCIDescribeAny_log_stat(imp_sth, imp_sth->svchp,imp_sth->errhp,obj->tdo,(ub4)0,OCI_OTYPE_PTR,(ub1)1,OCI_PTYPE_TYPE,imp_sth->dschp,status);
	/*we have the Actual TDO  so lets see what it is made up of by a describe*/

	if (status != OCI_SUCCESS) {
		oci_error(sth,imp_sth->errhp, status, "OCIDescribeAny");
		return 0;
	}

	OCIAttrGet_parmap(imp_sth, imp_sth->dschp,OCI_HTYPE_DESCRIBE,  &obj->parmdp, 0, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
		return 0;
	}

	/*and we store it in the object's paramdp for now*/

	OCIAttrGet_parmdp(imp_sth, obj->parmdp, &schema_name, &schema_namel, OCI_ATTR_SCHEMA_NAME, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
		return 0;
	}

	OCIAttrGet_parmdp(imp_sth, obj->parmdp, &type_name, &type_namel, OCI_ATTR_NAME, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
		return 0;
	}

	/* make full type_name: schema_name + "." + type_name */
	obj->full_type_name = newSVpv((char*)schema_name, schema_namel);
	sv_catpvn(obj->full_type_name, ".", 1);
	sv_catpvn(obj->full_type_name, (char*)type_name, type_namel);
	obj->type_name = (text*)SvPV(obj->full_type_name,PL_na);

	/*we need to know its type code*/

	OCIAttrGet_parmdp(imp_sth, obj->parmdp, (dvoid *)&obj->typecode, 0, OCI_ATTR_TYPECODE, status);

	if (status != OCI_SUCCESS) {
		oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
		return 0;
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 ) {
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "Getting the properties of object named =%s at level %d typecode=%d\n",
            obj->type_name,level,obj->typecode);
	}

	if (obj->typecode == OCI_TYPECODE_OBJECT || obj->typecode == OCI_TYPECODE_OPAQUE){
		OCIParam *list_attr= (OCIParam *) 0;
		ub2	  pos;
		if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 ) {
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "Object named =%s at level %d is an Object\n",
                obj->type_name,level);
		}

		OCIAttrGet_parmdp(imp_sth, obj->parmdp, (dvoid *)&obj->obj_ref, 0, OCI_ATTR_REF_TDO, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}
		/*we will need a reff to the TDO for the pin operation*/

		OCIObjectPin_log_stat(imp_sth, imp_sth->envhp,imp_sth->errhp, obj->obj_ref,(dvoid  **)&obj->obj_type,status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIObjectPin");
			return 0;
		}

		OCIAttrGet_parmdp(imp_sth,  obj->parmdp, (dvoid *)&obj->is_final_type,(ub4 *) 0, OCI_ATTR_IS_FINAL_TYPE, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}
		OCIAttrGet_parmdp(imp_sth,  obj->parmdp, (dvoid *)&obj->field_count,(ub4 *) 0, OCI_ATTR_NUM_TYPE_ATTRS, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}

		/*now get the differnt fields of this object add one field object for property*/
		Newz(1, obj->fields, (unsigned) obj->field_count, fbh_obj_t);

		/*a field is just another instance of an obj not a new struct*/

		OCIAttrGet_parmdp(imp_sth,  obj->parmdp, (dvoid *)&list_attr,(ub4 *) 0, OCI_ATTR_LIST_TYPE_ATTRS, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}


		for (pos = 1; pos <= obj->field_count; pos++){
			OCIParam *parmdf= (OCIParam *) 0;
			fbh_obj_t *fld = &obj->fields[pos-1]; /*get the field holder*/

			OCIParamGet_log_stat(imp_sth, (dvoid *) list_attr,(ub4) OCI_DTYPE_PARAM, imp_sth->errhp,(dvoid *)&parmdf, (ub4) pos ,status);

			if (status != OCI_SUCCESS) {
				oci_error(sth,imp_sth->errhp, status, "OCIParamGet");
				return 0;
			}

			OCIAttrGet_parmdp(imp_sth,  (dvoid*)parmdf, (dvoid *)&fld->type_name,(ub4 *) &fld->type_namel, OCI_ATTR_NAME, status);

			/* get the name of the attribute */

			if (status != OCI_SUCCESS) {
				oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
				return 0;
			}

				OCIAttrGet_parmdp(imp_sth,  (dvoid*)parmdf, (void *)&fld->typecode,(ub4 *) 0, OCI_ATTR_TYPECODE, status);

			if (status != OCI_SUCCESS) {
				oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
				return 0;
			}

			if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 ) {
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "Getting property #%d, named=%s and its typecode is %d \n",
                    pos, fld->type_name, fld->typecode);
			}

			if (fld->typecode == OCI_TYPECODE_OBJECT || fld->typecode == OCI_TYPECODE_VARRAY || fld->typecode == OCI_TYPECODE_TABLE || fld->typecode == OCI_TYPECODE_NAMEDCOLLECTION){
				 /*this is some sort of object or collection so lets drill down some more*/
				Newz(1, fld->fields, 1, fbh_obj_t);
				fld->field_count=1;/*not really needed but used internally*/
					status=describe_obj(sth,imp_sth,parmdf,fld->fields,level+1);
			}
		}
	} else {
		/*well this is an embedded table or varray of some form so find out what is in it*/

		if (DBIc_DBISTATE(imp_sth)->debug >= 6 || dbd_verbose >= 6 ) {
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "Object named =%s at level %d is an Varray or Table\n",
                obj->type_name,level);
		}

		OCIAttrGet_parmdp(imp_sth,  obj->parmdp, (dvoid *)&obj->col_typecode, 0, OCI_ATTR_COLLECTION_TYPECODE, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}
		/* first get what sort of collection it is by coll typecode*/
			OCIAttrGet_parmdp(imp_sth,  obj->parmdp, (dvoid *)&obj->parmap, 0, OCI_ATTR_COLLECTION_ELEMENT, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}

		OCIAttrGet_parmdp(imp_sth, obj->parmap, (dvoid *)&obj->element_typecode, 0, OCI_ATTR_TYPECODE, status);

		if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIAttrGet");
			return 0;
		}

		if (obj->element_typecode == OCI_TYPECODE_OBJECT || obj->element_typecode == OCI_TYPECODE_VARRAY || obj->element_typecode == OCI_TYPECODE_TABLE || obj->element_typecode == OCI_TYPECODE_NAMEDCOLLECTION){
			 /*this is some sort of object or collection so lets drill down some more*/
			fbh_obj_t *fld;
			Newz(1, obj->fields, 1, fbh_obj_t);
			fld = &obj->fields[0]; /*get the field holder*/
			obj->field_count=1; /*not really needed but used internally*/
				status=describe_obj(sth,imp_sth,obj->parmap,fld,level+1);
		}

	}
	return 1;

}


int
dump_struct(imp_sth_t *imp_sth,fbh_obj_t *obj,int level){
	dTHX;
	int i;
/*dumps the contents of the current fbh->obj*/

	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), " level=%d	type_name = %s\n",level,obj->type_name);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	type_namel = %u\n",obj->type_namel);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	parmdp = %p\n",obj->parmdp);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	parmap = %p\n",obj->parmap);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	tdo = %p\n",obj->tdo);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	typecode = %s\n",oci_typecode_name(obj->typecode));
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	col_typecode = %d\n",obj->col_typecode);
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth),
        "	element_typecode = %s\n",oci_typecode_name(obj->element_typecode));
	PerlIO_printf(
        DBIc_LOGPIO(imp_sth), "	obj_ref = %p\n",obj->obj_ref);
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	obj_value = %p\n",obj->obj_value);
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	obj_type = %p\n",obj->obj_type);
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	is_final_type = %u\n",obj->is_final_type);
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	field_count = %d\n",obj->field_count);
	PerlIO_printf(DBIc_LOGPIO(imp_sth), "	fields = %p\n",obj->fields);

	for (i = 0; i < obj->field_count;i++){
		fbh_obj_t *fld = &obj->fields[i];
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "  \n--->sub objects\n  ");
		dump_struct(imp_sth,fld,level+1);
	}

	PerlIO_printf(DBIc_LOGPIO(imp_sth), "  \n--->done %s\n  ",obj->type_name);

	return 1;
}





int
dbd_describe(SV *h, imp_sth_t *imp_sth)
{
	dTHX;
	D_imp_dbh_from_sth;
	D_imp_drh_from_dbh;
	UV	long_readlen;
	ub4 num_fields;
	int num_errors	= 0;
	int has_longs	= 0;
	int est_width	= 0;		/* estimated avg row width (for cache)	*/
	int nested_cursors = 0;
	ub4 i = 0;
	sword status;


	if (imp_sth->done_desc)
		return 1;	/* success, already done it */

	imp_sth->done_desc = 1;

	/* ora_trunc is checked at fetch time */
	/* long_readlen:	length for long/longraw (if >0), else 80 (ora app dflt)	*/
	/* Ought to be for COMPAT mode only but was relaxed before LongReadLen existed */
	long_readlen = (SvOK(imp_drh -> ora_long) && SvUV(imp_drh->ora_long)>0)
        ? SvUV(imp_drh->ora_long) : DBIc_LongReadLen(imp_sth);

	/* set long_readlen for SELECT or PL/SQL with output placeholders */
	imp_sth->long_readlen = long_readlen;


	if (imp_sth->stmt_type != OCI_STMT_SELECT) { /* XXX DISABLED, see num_fields test below */
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	dbd_describe skipped for %s\n",
				oci_stmt_type_name(imp_sth->stmt_type));
        /* imp_sth memory was cleared when created so no setup required here	*/
		return 1;
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	dbd_describe %s (%s, lb %lu)...\n",
			oci_stmt_type_name(imp_sth->stmt_type),
			DBIc_ACTIVE(imp_sth) ? "implicit" : "EXPLICIT", (unsigned long)long_readlen);

	/* We know it's a select and we've not got the description yet, so if the	*/
	/* sth is not 'active' (executing) then we need an explicit describe.	*/
	if ( !DBIc_ACTIVE(imp_sth) ) {

		OCIStmtExecute_log_stat(imp_sth, imp_sth->svchp, imp_sth->stmhp, imp_sth->errhp,
                                0, 0, 0, 0, OCI_DESCRIBE_ONLY, status);
		if (status != OCI_SUCCESS) {
			oci_error(h, imp_sth->errhp, status,
                      ora_sql_error(imp_sth, "OCIStmtExecute/Describe"));
			if (status != OCI_SUCCESS_WITH_INFO)
                return 0;
		}
	}
	OCIAttrGet_stmhp_stat(imp_sth, &num_fields, 0, OCI_ATTR_PARAM_COUNT, status);
	if (status != OCI_SUCCESS) {
		oci_error(h, imp_sth->errhp, status, "OCIAttrGet OCI_ATTR_PARAM_COUNT");
		return 0;
	}
	if (num_fields == 0) {
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	dbd_describe skipped for %s (no fields returned)\n",
                oci_stmt_type_name(imp_sth->stmt_type));
		/* imp_sth memory was cleared when created so no setup required here	*/
		return 1;
	}

	DBIc_NUM_FIELDS(imp_sth) = num_fields;
	Newz(42, imp_sth->fbh, num_fields, imp_fbh_t);

	/* Get number of fields and space needed for field names	*/
    /* loop though the fields and get all the fileds and thier types to get back*/

	for(i = 1; i <= num_fields; ++i) { /*start define of filed struct[i] fbh */
		char *p;
		ub4 atrlen;
		int avg_width	= 0;
		imp_fbh_t *fbh	= &imp_sth->fbh[i-1];
		fbh->imp_sth	 = imp_sth;
		fbh->field_num	= i;
		fbh->define_mode = OCI_DEFAULT;

		OCIParamGet_log_stat(imp_sth, imp_sth->stmhp, OCI_HTYPE_STMT, imp_sth->errhp,
                             (dvoid**)&fbh->parmdp, (ub4)i, status);

		if (status != OCI_SUCCESS) {
			oci_error(h, imp_sth->errhp, status, "OCIParamGet");
			return 0;
		}

		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->dbtype, 0, OCI_ATTR_DATA_TYPE, status);
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->dbsize, 0, OCI_ATTR_DATA_SIZE, status);
		/*may be a bug in 11 where the OCI_ATTR_DATA_SIZE my return 0 which should never happen*/
		/*to fix or kludge for this I added a little code for ORA_VARCHAR2 below */

#ifdef OCI_ATTR_CHAR_USED
		/* 0 means byte-length semantics, 1 means character-length semantics */
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->len_char_used, 0, OCI_ATTR_CHAR_USED, status);
		/* OCI_ATTR_CHAR_SIZE: like OCI_ATTR_DATA_SIZE but measured in chars	*/
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->len_char_size, 0, OCI_ATTR_CHAR_SIZE, status);
#endif
		fbh->csid = 0; fbh->csform = 0; /* just to be sure */
#ifdef OCI_ATTR_CHARSET_ID
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->csid,	0, OCI_ATTR_CHARSET_ID,	status);
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->csform, 0, OCI_ATTR_CHARSET_FORM, status);
#endif
        /* OCI_ATTR_PRECISION returns 0 for most types including some numbers		*/
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->prec,	0, OCI_ATTR_PRECISION, status);
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->scale,  0, OCI_ATTR_SCALE,	 status);
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->nullok, 0, OCI_ATTR_IS_NULL,	status);
		OCIAttrGet_parmdp(imp_sth, fbh->parmdp, &fbh->name,	&atrlen, OCI_ATTR_NAME,status);
		if (atrlen == 0) { /* long names can cause oracle to return 0 for atrlen */
			char buf[99];
			sprintf(buf,"field_%d_name_too_long", i);
			fbh->name = &buf[0];
			atrlen = strlen(fbh->name);
		}
		fbh->name_sv = newSVpv(fbh->name,atrlen);
		fbh->name	= SvPVX(fbh->name_sv);
		fbh->ftype	= 5;	/* default: return as null terminated string */

        /* TO_DO there is something wrong with the tracing below as sql_typecode_name
           returns NVARCHAR2 for type 2 and ORA_NUMBER is 2 */
		if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "Describe col #%d type=%d(%s)\n",
                i,fbh->dbtype,sql_typecode_name(fbh->dbtype));

		switch (fbh->dbtype) {
            /*	the simple types	*/
          case	ORA_VARCHAR2:				/* VARCHAR2	*/

            if (fbh->dbsize == 0){
                fbh->dbsize=4000;
            }
            avg_width = fbh->dbsize / 2;
            /* FALLTHRU */
          case	ORA_CHAR:				/* CHAR		*/
            if ( CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) && !CS_IS_UTF8(fbh->csid) )
                fbh->disize = fbh->dbsize * 4;
            else
                fbh->disize = fbh->dbsize;

            fbh->prec	= fbh->disize;
            break;
          case	ORA_RAW:				/* RAW		*/
            fbh->disize = fbh->dbsize * 2;
            fbh->prec	= fbh->disize;
            break;
          case	ORA_NUMBER:				/* NUMBER	*/
          case	21:				/* BINARY FLOAT os-endian	*/
          case	22:				/* BINARY DOUBLE os-endian	*/
          case	100:				/* BINARY FLOAT oracle-endian	*/
          case	101:				/* BINARY DOUBLE oracle-endian	*/
            fbh->disize = 130+38+3;		/* worst case	*/
            avg_width = 4;	 /* NUMBER approx +/- 1_000_000 */
            break;

          case	ORA_DATE:				/* DATE		*/
            /* actually dependent on NLS default date format*/
            fbh->disize = 75;	/* a generous default	*/
            fbh->prec	= fbh->disize;
            avg_width = 8;	/* size in SQL*Net packet  */
            break;

          case	ORA_LONG:				/* LONG		*/
            imp_sth->row_cache_off	= 1;
            has_longs++;
            if (imp_sth->clbk_lob){ /*get by peice with callback a slow*/

                fbh->clbk_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/

                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }

                fbh->ftype		= SQLT_CHR;
                fbh->fetch_func = fetch_clbk_lob;

            }
            else if (imp_sth->piece_lob){ /*get by peice with polling slowest*/

                fbh->piece_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/

                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }
                fbh->ftype = SQLT_CHR;
                fbh->fetch_func = fetch_get_piece;
            }
            else {

                if ( CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) && !CS_IS_UTF8(fbh->csid) )
                    fbh->disize = long_readlen * 4;
                else
                    fbh->disize = long_readlen;

                /* not governed by else: */
                fbh->dbsize = (fbh->disize>65535) ? 65535 : fbh->disize;
                fbh->ftype  = 94; /* VAR form */
                fbh->fetch_func = fetch_func_varfield;

            }
            break;
          case	ORA_LONGRAW:				/* LONG RAW	*/
            has_longs++;
            if (imp_sth->clbk_lob){ /*get by peice with callback a slow*/

                fbh->clbk_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/

                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }

                fbh->ftype = SQLT_BIN;
                fbh->fetch_func = fetch_clbk_lob;

            }
            else if (imp_sth->piece_lob){ /*get by peice with polling slowest*/

                fbh->piece_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/

                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }
                fbh->ftype = SQLT_BIN;
                fbh->fetch_func = fetch_get_piece;
            }
            else {
                fbh->disize = long_readlen * 2;
                fbh->dbsize = (fbh->disize>65535) ? 65535 : fbh->disize;
                fbh->ftype  = 95; /* VAR form */
                fbh->fetch_func = fetch_func_varfield;
            }
            break;

          case	ORA_ROWID:				/* ROWID	*/
            fbh->disize = 20;
            fbh->prec	= fbh->disize;
            break;
          case	104:				/* ROWID Desc	*/
            fbh->disize = 2000;
            fbh->prec	= fbh->disize;
            break;
          case	108:				 /* some sort of embedded object */
            imp_sth->row_cache_off	= 1;/* cant fetch more thatn one at a time */
            fbh->ftype  = fbh->dbtype;  /*varray or alike */
            fbh->fetch_func = fetch_func_oci_object; /* need a new fetch function for it */
            fbh->fetch_cleanup = fetch_cleanup_oci_object; /* clean up any AV  from the fetch*/
            fbh->desc_t = SQLT_NTY;
            if (!imp_sth->dschp){
                OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
                if (status != OCI_SUCCESS) {
                    oci_error(h,imp_sth->errhp, status, "OCIHandleAlloc");
                    ++num_errors;
                }
            }
            break;
          case	ORA_CLOB:			/* CLOB	& NCLOB	*/
          case	ORA_BLOB:			/* BLOB		*/
          case	ORA_BFILE:			/* BFILE	*/
            has_longs++;
            fbh->ftype  	  		= fbh->dbtype;
            imp_sth->ret_lobs 		= 1;
            imp_sth->row_cache_off	= 1; /* Cannot use mulit fetch for a lob*/
            /* Unless they are just getting the locator */

            if (imp_sth->pers_lob){  /*get as one peice fasted but limited to 64k big you can get.*/

                fbh->pers_lob	= 1;

                if (long_readlen){
                    fbh->disize 	=long_readlen;/*user set max value for the fetch*/
                }
                else {
                    fbh->disize 	= fbh->dbsize*10; /*default size*/
                }


                if (fbh->dbtype == ORA_CLOB){
                    fbh->ftype  = SQLT_CHR;/*SQLT_LNG*/
                }
                else {
                    fbh->ftype = SQLT_LVB; /*Binary form seems this is the only value where we can get the length correctly*/
                }
            }
            else if (imp_sth->clbk_lob){ /*get by peice with callback a slow*/
                fbh->clbk_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/
                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }
                if (fbh->dbtype == ORA_CLOB){
                    fbh->ftype = SQLT_CHR;
                } else {
                    fbh->ftype = SQLT_BIN; /*other Binary*/
                }
                fbh->fetch_func = fetch_clbk_lob;

            }
            else if (imp_sth->piece_lob){ /*get by peice with polling slowest*/
                fbh->piece_lob		= 1;
                fbh->define_mode	= OCI_DYNAMIC_FETCH; /* piecwise fetch*/
                fbh->disize 		= imp_sth->long_readlen; /*user set max value for the fetch*/
                fbh->piece_size		= imp_sth->piece_size; /*the size for each piece*/
                fbh->fetch_cleanup 	= fetch_cleanup_pres_lobs; /* clean up buffer before each fetch*/
                if (!imp_sth->piece_size){ /*if not set use max value*/
                    imp_sth->piece_size=imp_sth->long_readlen;
                }
                if (fbh->dbtype == ORA_CLOB){
                    fbh->ftype = SQLT_CHR;
                }
                else {
                    fbh->ftype = SQLT_BIN; /*other Binary */
                }
                fbh->fetch_func = fetch_get_piece;

            }
            else { /*auto lob fetch with locator by far the fastest*/
                fbh->disize =  sizeof(OCILobLocator*);/* Size of the lob locator ar we do not really get the lob! */
                if (imp_sth->auto_lob) {
                    fbh->fetch_func = fetch_func_autolob;
                }
                else {
                    fbh->fetch_func = fetch_func_getrefpv;
                }

                fbh->bless  = "OCILobLocatorPtr";
                fbh->desc_t = OCI_DTYPE_LOB;
                OCIDescriptorAlloc_ok(imp_sth, imp_sth->envhp, &fbh->desc_h, fbh->desc_t);


            }

            break;

#ifdef OCI_DTYPE_REF
          case	111:				/* REF		*/
            fbh_setup_getrefpv(imp_sth, fbh, OCI_DTYPE_REF, "OCIRefPtr");
            break;
#endif

          case	ORA_RSET:				/* RSET		*/
            fbh->ftype  = fbh->dbtype;
            fbh->disize = sizeof(OCIStmt *);
            fbh->fetch_func = fetch_func_rset;
            fbh->fetch_cleanup = fetch_cleanup_rset;
            nested_cursors++;
            break;

          case	182:				  /* INTERVAL YEAR TO MONTH */
          case	183:				  /* INTERVAL DAY TO SECOND */
          case	185:				  /* TIME (ocidfn.h) */
          case	186:				  /* TIME WITH TIME ZONE (ocidfn.h) */
          case	187:				  /* TIMESTAMP */
          case	188: 				/* TIMESTAMP WITH TIME ZONE	*/
          case	189:				  /* INTERVAL YEAR TO MONTH (ocidfn.h) */
          case	190:				  /* INTERVAL DAY TO SECOND */
          case	232:				  /* TIMESTAMP WITH LOCAL TIME ZONE */
            /* actually dependent on NLS default date format*/
            fbh->disize = 75;		/* XXX */
            break;

          default:
			/* XXX unhandled type may lead to errors or worse */
            fbh->ftype  = fbh->dbtype;
            fbh->disize = fbh->dbsize;
            p = "Field %d has an Oracle type (%d) which is not explicitly supported%s";
            if (DBIc_DBISTATE(imp_sth)->debug >= 1 || dbd_verbose >= 3 )
                PerlIO_printf(DBIc_LOGPIO(imp_sth), p, i, fbh->dbtype, "\n");
            if (PL_dowarn)
                warn(p, i, fbh->dbtype, "");
            break;
		}

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
            PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "Described col %2d: dbtype %d(%s), scale %d, prec %d, nullok %d, "
                "name %s\n		  : dbsize %d, char_used %d, char_size %d, "
                "csid %d, csform %d(%s), disize %d\n",
                i, fbh->dbtype, sql_typecode_name(fbh->dbtype), fbh->scale,
                fbh->prec, fbh->nullok, fbh->name, fbh->dbsize,
                fbh->len_char_used, fbh->len_char_size,
                fbh->csid,fbh->csform,oci_csform_name(fbh->csform), fbh->disize);

		if (fbh->ftype == 5)	/* XXX need to handle wide chars somehow */
			fbh->disize += 1;	/* allow for null terminator */

        /* dbsize can be zero for 'select NULL ...'			*/

		imp_sth->t_dbsize += fbh->dbsize;

		if (!avg_width)
			avg_width = fbh->dbsize;

		est_width += avg_width;

		if (DBIc_DBISTATE(imp_sth)->debug >= 2 || dbd_verbose >= 3 )
			dbd_fbh_dump(imp_sth, fbh, (int)i, 0);

	}/* end define of filed struct[i] fbh*/

	imp_sth->est_width = est_width;

	sth_set_row_cache(h, imp_sth,
                      (imp_dbh->max_nested_cursors) ? 0 :nested_cursors ,
                      (int)num_fields, has_longs );
	/* Initialise cache counters */
	imp_sth->in_cache  = 0;
	imp_sth->eod_errno = 0;
	/*rs_array_init(imp_sth);*/



	/* now set up the oci call with define by pos*/
	for(i=1; i <= num_fields; ++i) {
		imp_fbh_t *fbh = &imp_sth->fbh[i-1];
		int ftype = fbh->ftype;
		/* add space for STRING null term, or VAR len prefix */
		sb4 define_len = (ftype==94||ftype==95) ? fbh->disize+4 : fbh->disize;
		fb_ary_t  *fb_ary;

		if (fbh->clbk_lob || fbh->piece_lob  ){/*init the cb_abuf with this call*/
			fbh->fb_ary = fb_ary_cb_alloc(imp_sth->piece_size,define_len, imp_sth->rs_array_size);

		} else {
			fbh->fb_ary = fb_ary_alloc(define_len, imp_sth->rs_array_size);
		}

		fb_ary = fbh->fb_ary;

		if (fbh->ftype == SQLT_BIN)  {
			define_len++;
			/*add one extra byte incase the size of the lob is equal to the define_len*/
		}

		if (fbh->ftype == ORA_RSET) { /* RSET */
			OCIHandleAlloc_ok(imp_sth, imp_sth->envhp,
                              (dvoid*)&((OCIStmt **)fb_ary->abuf)[0],
                              OCI_HTYPE_STMT, status);
		}

		OCIDefineByPos_log_stat(imp_sth, imp_sth->stmhp,
                                &fbh->defnp,
                                imp_sth->errhp,
                                (ub4) i,
                                (fbh->desc_h) ? (dvoid*)&fbh->desc_h : fbh->clbk_lob  ? (dvoid *) 0: fbh->piece_lob  ? (dvoid *) 0:(dvoid*)fb_ary->abuf,
                                (fbh->desc_h) ?					0 :		define_len,
                                (ub2)fbh->ftype,
                                fb_ary->aindp,
                                (ftype==94||ftype==95) ? NULL : fb_ary->arlen,
                                fb_ary->arcode,
                                fbh->define_mode,
                                status);


		if (fbh->clbk_lob){
            /* use a dynamic callback for persistent binary and char lobs*/
			OCIDefineDynamic_log_stat(imp_sth, fbh->defnp,imp_sth->errhp,(dvoid *) fbh,status);
		}

		if (fbh->ftype == 108)  { /* Embedded object bind it differently*/
			if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ){
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "Field #%d is a  object or colection of some sort. "
                    "Using OCIDefineObject and or OCIObjectPin \n",i);
			}
			Newz(1, fbh->obj, 1, fbh_obj_t);
			fbh->obj->typecode=fbh->dbtype;
			if (!describe_obj(h,imp_sth,fbh->parmdp,fbh->obj,0)){
				++num_errors;
			}

			if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ){
				dump_struct(imp_sth,fbh->obj,0);
			}
			OCIDefineObject_log_stat(imp_sth,fbh->defnp,imp_sth->errhp,fbh->obj->tdo,(dvoid**)&fbh->obj->obj_value,(dvoid**)&fbh->obj->obj_ind,status);

			if (status != OCI_SUCCESS) {
				oci_error(h,imp_sth->errhp, status, "OCIDefineObject");
				++num_errors;
			}

		}

		if (status != OCI_SUCCESS) {
			oci_error(h, imp_sth->errhp, status, "OCIDefineByPos");
			++num_errors;
		}


#ifdef OCI_ATTR_CHARSET_FORM
		if ( (fbh->dbtype == 1) && fbh->csform ) {
            /* csform may be 0 when talking to Oracle 8.0 database*/
			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	calling OCIAttrSet OCI_ATTR_CHARSET_FORM with csform=%d (%s)\n",
                    fbh->csform,oci_csform_name(fbh->csform) );
            OCIAttrSet_log_stat(imp_sth, fbh->defnp, (ub4) OCI_HTYPE_DEFINE, (dvoid *) &fbh->csform,
                                (ub4) 0, (ub4) OCI_ATTR_CHARSET_FORM, imp_sth->errhp, status );
			if (status != OCI_SUCCESS) {
				oci_error(h, imp_sth->errhp, status, "OCIAttrSet OCI_ATTR_CHARSET_FORM");
				++num_errors;
			}
		}
#endif /* OCI_ATTR_CHARSET_FORM */

	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
			"	dbd_describe'd %d columns (row bytes: %d max, %d est avg, cache: %d)\n",
			(int)num_fields, imp_sth->t_dbsize, imp_sth->est_width,
            imp_sth->prefetch_rows);

	return (num_errors>0) ? 0 : 1;
}


AV *
dbd_st_fetch(SV *sth, imp_sth_t *imp_sth){
	dTHX;
    D_imp_xxh(sth);
	sword status;
	D_imp_dbh_from_sth;
	int num_fields = DBIc_NUM_FIELDS(imp_sth);
	int ChopBlanks;
	int err;
	int i;
	AV *av;


	/* Check that execute() was executed sucessfully. This also implies	*/
	/* that dbd_describe() executed sucessfuly so the memory buffers	*/
	/* are allocated and bound.						*/
	if ( !DBIc_ACTIVE(imp_sth) ) {
		oci_error(sth, NULL, OCI_ERROR, imp_sth->nested_cursor ?
		"nested cursor is defunct (parent row is no longer current)" :
		"no statement executing (perhaps you need to call execute first)");
		return Nullav;
	}

	for(i=0; i < num_fields; ++i) {
		imp_fbh_t *fbh = &imp_sth->fbh[i];
		if (fbh->fetch_cleanup)
			fbh->fetch_cleanup(sth, fbh);
	}

	if (ora_fetchtest && DBIc_ROW_COUNT(imp_sth)>0) {
		--ora_fetchtest; /* trick for testing performance */
		status = OCI_SUCCESS;
	}
	else {
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 ){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	dbd_st_fetch %d fields...\n", DBIc_NUM_FIELDS(imp_sth));
		}

		if (imp_sth->fetch_orient != OCI_DEFAULT) {
			if (imp_sth->exe_mode!=OCI_STMT_SCROLLABLE_READONLY)
				croak ("attempt to use a scrollable cursor without first setting ora_exe_mode to OCI_STMT_SCROLLABLE_READONLY\n") ;

			if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	Scrolling Fetch, position before fetch=%d, "
                    "Orientation = %s , Fetchoffset =%d\n",
					imp_sth->fetch_position, oci_fetch_options(imp_sth->fetch_orient),
                    imp_sth->fetch_offset);

			OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp,1, imp_sth->fetch_orient,imp_sth->fetch_offset, status);
				/*this will work without a round trip so might as well open it up for all statments handles*/
				/* default and OCI_FETCH_NEXT are the same so this avoids miscaluation on the next value*/
			if (status==OCI_NO_DATA){
                return Nullav;
            }

			OCIAttrGet_stmhp_stat(imp_sth, &imp_sth->fetch_position, 0, OCI_ATTR_CURRENT_POSITION, status);

			if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	Scrolling Fetch, postion after fetch=%d\n",
                    imp_sth->fetch_position);
		}
		else {

			if (imp_sth->row_cache_off){ /*Do not use array fetch or local cache */
				OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp, imp_sth->errhp,1,(ub2)OCI_FETCH_NEXT, OCI_DEFAULT, status);
				imp_sth->rs_fetch_count++;
				imp_sth->rs_array_idx=0;

			}
			else {  /*Array Fetch the New Normal Super speedy and very nice*/


 				imp_sth->rs_array_idx++;
				if (imp_sth->rs_array_num_rows<=imp_sth->rs_array_idx && (imp_sth->rs_array_status==OCI_SUCCESS || imp_sth->rs_array_status==OCI_SUCCESS_WITH_INFO)) {
/* 			PerlIO_printf(DBIc_LOGPIO(imp_sth), "	dbd_st_fetch fields...b\n");*/

					OCIStmtFetch_log_stat(imp_sth, imp_sth->stmhp,imp_sth->errhp,imp_sth->rs_array_size,(ub2)OCI_FETCH_NEXT,OCI_DEFAULT,status);

					imp_sth->rs_array_status=status;
					imp_sth->rs_fetch_count++;
					if (oci_warn &&  (imp_sth->rs_array_status == OCI_SUCCESS_WITH_INFO)) {
						oci_error(sth, imp_sth->errhp, status, "OCIStmtFetch");
					}
					OCIAttrGet_stmhp_stat(imp_sth, &imp_sth->rs_array_num_rows,0,OCI_ATTR_ROWS_FETCHED, status);
					imp_sth->rs_array_idx=0;
					imp_dbh->RowsInCache =imp_sth->rs_array_size;
					imp_sth->RowsInCache =imp_sth->rs_array_size;

					if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 || oci_warn)
						PerlIO_printf(
                            DBIc_LOGPIO(imp_sth),
                            "...Fetched %d rows\n",imp_sth->rs_array_num_rows);

				}
				imp_dbh->RowsInCache--;
			    imp_sth->RowsInCache--;




				if (imp_sth->rs_array_num_rows>imp_sth->rs_array_idx)	/* set status to success if rows in cache */
					status=OCI_SUCCESS;
				else
					status=imp_sth->rs_array_status;
			}
		}
	}

	if (status != OCI_SUCCESS && status !=OCI_NEED_DATA) {
		ora_fetchtest = 0;

		if (status == OCI_NO_DATA) {
			dTHR; 			/* for DBIc_ACTIVE_off	*/
			DBIc_ACTIVE_off(imp_sth);	/* eg finish		*/
			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 || oci_warn)
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	dbd_st_fetch no-more-data, fetch count=%d\n",
                    imp_sth->rs_fetch_count-1);
			return Nullav;
		}
		if (status != OCI_SUCCESS_WITH_INFO) {
			dTHR; 			/* for DBIc_ACTIVE_off	*/
			DBIc_ACTIVE_off(imp_sth);	/* eg finish		*/
			oci_error(sth, imp_sth->errhp, status, "OCIStmtFetch");
			return Nullav;
		}
		if (oci_warn && (status == OCI_SUCCESS_WITH_INFO)) {
			oci_error(sth, imp_sth->errhp, status, "OCIStmtFetch");
		}


	/* for OCI_SUCCESS_WITH_INFO we fall through and let the	*/
	/* per-field rcode value be dealt with as we fetch the data	*/
	}

	av = DBIc_DBISTATE(imp_sth)->get_fbav(imp_sth);

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 ) {
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "	dbd_st_fetched %d fields with status of %d(%s)\n",
            num_fields,status, oci_status_name(status));
	}

	ChopBlanks = DBIc_has(imp_sth, DBIcf_ChopBlanks);
	err = 0;

	for(i=0; i < num_fields; ++i) {
		imp_fbh_t *fbh		= &imp_sth->fbh[i];
		fb_ary_t *fb_ary	= fbh->fb_ary;
		int rc 				= fb_ary->arcode[imp_sth->rs_array_idx];
		ub1* row_data		= &fb_ary->abuf[0]+(fb_ary->bufl*imp_sth->rs_array_idx);
		SV *sv 				= AvARRAY(av)[i]; /* Note: we (re)use the SV in the AV	*/;


		if (DBIc_DBISTATE(imp_sth)->debug >= 4 || dbd_verbose >= 4 ) {
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "	field #%d with rc=%d(%s)\n",i+1,rc,oci_col_return_codes(rc));
		}

		if (rc == 1406				/* field was truncated	*/
			&& ora_dbtype_is_long(fbh->dbtype)/* field is a LONG	*/
		){
			int oraperl = DBIc_COMPAT(imp_sth);
			D_imp_dbh_from_sth ;
			D_imp_drh_from_dbh ;
			if (DBIc_has(imp_sth,DBIcf_LongTruncOk) || (oraperl && SvIV(imp_drh -> ora_trunc))) {
			/* user says truncation is ok */
			/* Oraperl recorded the truncation in ora_errno so we	*/
			/* so also but only for Oraperl mode handles.		*/
				if (oraperl) sv_setiv(DBIc_ERR(imp_sth), (IV)rc);
					rc = 0;		/* but don't provoke an error here	*/
			}
		/* else fall through and let rc trigger failure below	*/
		}

		if  (rc == 0	|| 	/* the normal case*/
			(rc == 1406 && DBIc_has(imp_sth,DBIcf_LongTruncOk))/*Field Truncaded*/) {

			if (fbh->fetch_func) {
 				if (!fbh->fetch_func(sth, fbh, sv)){
					++err;	/* fetch_func already called oci_error */
				}
			}
			else {
				int datalen = fb_ary->arlen[imp_sth->rs_array_idx];
				char *p = (char*)row_data;
                if (rc == 1406 ){
			        datalen= fbh->disize;
				}


				if (fbh->ftype == SQLT_LVB){
					/* very special case for binary lobs that are directly fetched.
						Seems I have to use SQLT_LVB to get the length all other will fail*/
					datalen = *(ub4*)row_data;
					sv_setpvn(sv, (char*)row_data+ sizeof(ub4), datalen);
				}
				else {
					if (ChopBlanks && fbh->dbtype == 96) {
						while(datalen && p[datalen - 1]==' ')
							--datalen;
					}
					sv_setpvn(sv, p, (STRLEN)datalen);
#if DBIXS_REVISION > 13590
		/* If a bind type was specified we use DBI's sql_type_cast
			to cast it - currently only number types are handled */
					if ((fbh->req_type != 0) && (fbh->bind_flags != 0)) {
						int sts;
						char errstr[256];

						sts = DBIc_DBISTATE(imp_sth)->sql_type_cast_svpv(
                            aTHX_ sv, fbh->req_type, fbh->bind_flags, NULL);

						if (sts == 0) {
							sprintf(errstr,
								"over/under flow converting column %d to type %"IVdf"",
								i+1, fbh->req_type);
							oci_error(sth, imp_sth->errhp, OCI_ERROR, errstr);
							return Nullav;

						}
						else if (sts == -2) {
							sprintf(errstr,
								"unsupported bind type %"IVdf" for column %d",
								fbh->req_type, i+1);
                            /* issue warning */
                            DBIh_SET_ERR_CHAR(sth, imp_xxh, "0", 1, errstr, Nullch, Nullch);
                            if (CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) ){
                                SvUTF8_on(sv);
                            }
						}
					}
					else
#endif /* DBISTATE_VERSION > 94 */
					{
						if (CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform) ){
							SvUTF8_on(sv);
						}
					}
				}
			}

		}
		else if (rc == 1405) {	/* field is null - return undef	*/
			sv_set_undef(sv);
		}
		else {  /* See odefin rcode arg description in OCI docs	*/
			char buf[200];
			char *hint = "";
			/* These may get more case-by-case treatment eventually.	*/
			if (rc == 1406) { /* field truncated (see above)  */
				if (!fbh->fetch_func) {
					/* Copy the truncated value anyway, it may be of use,	*/
					/* but it'll only be accessible via prior bind_column()	*/
					sv_setpvn(sv, (char *)row_data,fb_ary->arlen[imp_sth->rs_array_idx]);
 					if ((CSFORM_IMPLIES_UTF8(imp_dbh, fbh->csform)) && (fbh->ftype != SQLT_BIN)){
						SvUTF8_on(sv);
					}
				}

				if (ora_dbtype_is_long(fbh->dbtype)){	/* double check */
					hint = ", LongReadLen too small and/or LongTruncOk not set";
				}

			}
			else {	/* set field that caused error to undef */
				sv_set_undef(sv);
			}
			++err;	/* 'fail' this fetch but continue getting fields */
					/* Some should probably be treated as warnings but	*/
					/* for now we just treat them all as errors		*/
			sprintf(buf,"ORA-%05d error on field %d of %d, ora_type %d%s",rc, i+1, num_fields, fbh->dbtype, hint);
			oci_error(sth, imp_sth->errhp, OCI_ERROR, buf);
		}

		if (DBIc_DBISTATE(imp_sth)->debug >= 5 || dbd_verbose >= 5 ){
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "\n		%p (field=%d): %s\n",	 av, i,neatsvpv(sv,10));
		}
	}
	return (err) ? Nullav : av;
}


ub4
ora_parse_uid(imp_dbh_t *imp_dbh, char **uidp, char **pwdp)
{
	dTHX;
	sword status;

	/* OCI 8 does not seem to allow uid to be "name/pass" :-( */
	/* so we have to split it up ourselves */
	if (strlen(*pwdp)==0 && strchr(*uidp,'/')) {
		SV *tmpsv	= sv_2mortal(newSVpv(*uidp,0));
		*uidp 		= SvPVX(tmpsv);
		*pwdp 		= strchr(*uidp, '/');
		*(*pwdp)++ 	= '\0';
		/* XXX look for '@', e.g. "u/p@d" and "u@d" and maybe "@d"? */
	}
	if (**uidp == '\0' && **pwdp == '\0') {
		return OCI_CRED_EXT;
	}
#ifdef ORA_OCI_112
	if (0 == (imp_dbh->flags & ORA_IMP_DBH_USING_DRCP)) {
#endif
		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,
				*uidp, strlen(*uidp),
				(ub4) OCI_ATTR_USERNAME, imp_dbh->errhp, status);

		OCIAttrSet_log_stat(imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,
				(strlen(*pwdp)) ? *pwdp : NULL, strlen(*pwdp),
			(ub4) OCI_ATTR_PASSWORD, imp_dbh->errhp, status);
#ifdef ORA_OCI_112
	}
#endif
	return OCI_CRED_RDBMS;
}


int
ora_db_reauthenticate(SV *dbh, imp_dbh_t *imp_dbh, char *uid, char *pwd)
{
	dTHX;
	sword status;
#ifdef ORA_OCI_112
	if (imp_dbh->flags & ORA_IMP_DBH_USING_DRCP) return 0;
#endif
	/* XXX should possibly create new session before ending the old so	*/
	/* that if the new one can't be created, the old will still work.	*/
	OCISessionEnd_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp,
			imp_dbh->seshp, OCI_DEFAULT, status); /* XXX check status here?*/
	OCISessionBegin_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, imp_dbh->seshp,
			 ora_parse_uid(imp_dbh, &uid, &pwd), (ub4) OCI_DEFAULT, status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCISessionBegin");
		return 0;
	}
	return 1;
}


#ifdef not_used_curently
static char *
rowid2hex(OCIRowid *rowid)
{
	int i;
	SV *sv = sv_2mortal(newSVpv("",0));
	for (i = 0; i < OCI_ROWID_LEN; i++) {
		char buf[6];
		sprintf(buf, "%02X ", (int)(((ub1*)rowid)[i]));
		sv_catpv(sv, buf);
	}
	return SvPVX(sv);
}
#endif


static void *
alloc_via_sv(STRLEN len, SV **svp, int mortal)
{
	dTHX;
	SV *sv = newSVpv("",0);
	sv_grow(sv, len+1);
	memset(SvPVX(sv), 0, len);
	if (mortal)
	sv_2mortal(sv);
	if (svp)
	*svp = sv;
	return SvPVX(sv);
}


char *
find_ident_after(char *src, char *after, STRLEN *len, int copy)
{

	int seen_key = 0;
	char *orig = src;
	char *p;


	while(*src){
		if (*src == '\'') {
			char delim = *src;
			while(*src && *src != delim) ++src;
		}
		else if (*src == '-' && src[1] == '-') {
			while(*src && *src != '\n') ++src;
		}
		else if (*src == '/' && src[1] == '*') {
			while(*src && !(*src == '*' && src[1]=='/')) ++src;
		}
		else if (isALPHA(*src)) {
			if (seen_key) {
				char *start = src;
				while(*src && (isALNUM(*src) || *src=='.' || *src=='$' || *src=='"'))
					++src;
				*len = src - start;
				if (copy) {
					p = (char*)alloc_via_sv(*len, 0, 1);
					strncpy(p, start, *len);
					p[*len] = '\0';
					return p;
				}
				return start;
			}
			else if (  toLOWER(*src)==toLOWER(*after)
					&& (src==orig ? 1 : !isALPHA(src[-1]))) {
				p = after;
				while(*p && *src && toLOWER(*p)==toLOWER(*src))
					++p, ++src;
				if (!*p)
					seen_key = 1;
			}
			++src;
		}
	else
		++src;
	}
	return NULL;
}




struct lob_refetch_st {
	OCIStmt *stmthp;
	OCIBind *bindhp;
	OCIRowid *rowid;
	OCIParam *parmdp_tmp;
	OCIParam *parmdp_lob;
	int num_fields;
	SV *fbh_ary_sv;
	imp_fbh_t *fbh_ary;
};


static int
init_lob_refetch(SV *sth, imp_sth_t *imp_sth)
{
	dTHX;
	D_imp_dbh_from_sth;
	SV *sv;
	SV *sql_select;
	HV *lob_cols_hv = NULL;
	sword status;
	OCIError *errhp = imp_sth->errhp;
	OCIParam *parmhp = NULL, *collisthd = NULL, *colhd = NULL;
	ub2 numcols = 0;
	imp_fbh_t *fbh;
	int unmatched_params;
	I32 i,j;
	char *p;
	lob_refetch_t *lr = NULL;
	STRLEN tablename_len;
	char *tablename;
	char new_tablename[100];
	switch (imp_sth->stmt_type) {
		case OCI_STMT_UPDATE:
			tablename = find_ident_after(imp_sth->statement,
				"update", &tablename_len, 1);
			break;
		case OCI_STMT_INSERT:
			tablename = find_ident_after(imp_sth->statement,
				"into", &tablename_len, 1);
			break;
		default:
		return oci_error(sth, errhp, OCI_ERROR,
			"LOB refetch attempted for unsupported statement type (see also ora_auto_lob attribute)");
	}

	if (!tablename)
		return oci_error(sth, errhp, OCI_ERROR,
		"Unable to parse table name for LOB refetch");

 	if (!imp_sth->dschp){
        OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
			if (status != OCI_SUCCESS) {
			oci_error(sth,imp_sth->errhp, status, "OCIHandleAlloc");
		}

	 }

	OCIDescribeAny_log_stat(imp_sth, imp_sth->svchp, errhp, tablename, strlen(tablename),
		(ub1)OCI_OTYPE_NAME, (ub1)1, (ub1)OCI_PTYPE_SYN, imp_sth->dschp, status);

	if (status == OCI_SUCCESS) { /* There is a synonym, get the schema */
		char *syn_schema=NULL;
		char *syn_name;
		ub4  tn_len = 0, syn_schema_len = 0;

                Newx(syn_name, 1 + strlen(tablename), char);

		strcpy(syn_name,tablename);
		/* Put the synonym name here for later user */

		OCIAttrGet_log_stat(imp_sth, imp_sth->dschp,  OCI_HTYPE_DESCRIBE,
				&parmhp, 0, OCI_ATTR_PARAM, errhp, status);

		OCIAttrGet_log_stat(imp_sth, parmhp, OCI_DTYPE_PARAM,
				&syn_schema, &syn_schema_len, OCI_ATTR_SCHEMA_NAME, errhp, status);


		OCIAttrGet_log_stat(imp_sth, parmhp, OCI_DTYPE_PARAM,
				&tablename, &tn_len, OCI_ATTR_NAME, errhp, status);

		strncpy(new_tablename,syn_schema,syn_schema_len);
		new_tablename[syn_schema_len+1] = '\0';
		new_tablename[syn_schema_len]='.';
		strncat(new_tablename, tablename,tn_len);

		tablename=new_tablename;

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "		lob refetch using a synonym named=%s for %s \n",
                syn_name,tablename);

                Safefree(syn_name);
	}
	OCIDescribeAny_log_stat(imp_sth, imp_sth->svchp, errhp, tablename, strlen(tablename),
		(ub1)OCI_OTYPE_NAME, (ub1)1, (ub1)OCI_PTYPE_TABLE, imp_sth->dschp, status);

	if (status != OCI_SUCCESS) {
	/* XXX this OCI_PTYPE_TABLE->OCI_PTYPE_VIEW fallback should actually be	*/
	/* a loop that includes synonyms etc */
		OCIDescribeAny_log_stat(imp_sth, imp_sth->svchp, errhp, tablename, strlen(tablename),
			(ub1)OCI_OTYPE_NAME, (ub1)1, (ub1)OCI_PTYPE_VIEW, imp_sth->dschp, status);
		if (status != OCI_SUCCESS) {
			OCIHandleFree_log_stat(imp_sth, imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
			return oci_error(sth, errhp, status, "OCIDescribeAny(view)/LOB refetch");
		}
	}

	OCIAttrGet_log_stat(imp_sth, imp_sth->dschp,  OCI_HTYPE_DESCRIBE,
				&parmhp, 0, OCI_ATTR_PARAM, errhp, status);
	if (!status ) {
		OCIAttrGet_log_stat(imp_sth, parmhp, OCI_DTYPE_PARAM,
				&numcols, 0, OCI_ATTR_NUM_COLS, errhp, status);
	}

	if (!status ) {
		OCIAttrGet_log_stat(imp_sth, parmhp, OCI_DTYPE_PARAM,
				&collisthd, 0, OCI_ATTR_LIST_COLUMNS, errhp, status);
	}

	if (status != OCI_SUCCESS) {
		OCIHandleFree_log_stat(imp_sth, imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
		return oci_error(sth, errhp, status, "OCIDescribeAny/OCIAttrGet/LOB refetch");
	}

	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
            "		lob refetch from table %s, %d columns:\n",
            tablename, numcols);

	for (i = 1; i <= (long)numcols; i++) {
		ub2 col_dbtype;
		char *col_name;
		ub4  col_name_len;
		OCIParamGet_log_stat(imp_sth, collisthd, OCI_DTYPE_PARAM, errhp, (dvoid**)&colhd, i, status);
		if (status)
			break;

		OCIAttrGet_log_stat(imp_sth, colhd, OCI_DTYPE_PARAM, &col_dbtype, 0,
							OCI_ATTR_DATA_TYPE, errhp, status);
		if (status)
			break;

		OCIAttrGet_log_stat(imp_sth, colhd, OCI_DTYPE_PARAM, &col_name, &col_name_len,
				OCI_ATTR_NAME, errhp, status);
		if (status)
			break;

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
                "		lob refetch table col %d: '%.*s' otype %d\n",
				(int)i, (int)col_name_len,col_name, col_dbtype);

		if (col_dbtype != SQLT_CLOB && col_dbtype != SQLT_BLOB)
			continue;

		if (!lob_cols_hv)
			lob_cols_hv = newHV();

		sv = newSViv(col_dbtype);
		(void)sv_setpvn(sv, col_name, col_name_len);

		if (CSFORM_IMPLIES_UTF8(imp_dbh, SQLCS_IMPLICIT))
			SvUTF8_on(sv);

		(void)SvIOK_on(sv);	/* "what a wonderful hack!" */
		(void)hv_store(lob_cols_hv, col_name,col_name_len, sv,0);
		OCIDescriptorFree_log(imp_sth, colhd, OCI_DTYPE_PARAM);
		colhd = NULL;
	}

	if (colhd)
		OCIDescriptorFree_log(imp_sth, colhd, OCI_DTYPE_PARAM);

	if (status != OCI_SUCCESS) {
		oci_error(sth, errhp, status,
			"OCIDescribeAny/OCIParamGet/OCIAttrGet/LOB refetch");
		OCIHandleFree_log_stat(imp_sth, imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);
		return 0;
	}

	if (!lob_cols_hv)
		return oci_error(sth, errhp, OCI_ERROR,
			"LOB refetch failed, no lobs in table");

	/*	our bind params are in %imp_sth->all_params_hv
	our table cols are in %lob_cols_hv
	we now iterate through our bind params
	and allocate them to the appropriate table columns
	*/
	Newz(1, lr, 1, lob_refetch_t);
	unmatched_params = 0;
	lr->num_fields = 0;
	lr->fbh_ary = (imp_fbh_t*)alloc_via_sv(sizeof(imp_fbh_t) * HvKEYS(lob_cols_hv)+1,
	&lr->fbh_ary_sv, 0);

	sql_select = sv_2mortal(newSVpv("select ",0));

	hv_iterinit(imp_sth->all_params_hv);
	while( (sv = hv_iternextsv(imp_sth->all_params_hv, &p, &i)) != NULL ) {
		int matched = 0;
		phs_t *phs = (phs_t*)(void*)SvPVX(sv);

		if (sv == &PL_sv_undef || !phs)
			croak("panic: unbound params");

		if (phs->ftype != SQLT_CLOB && phs->ftype != SQLT_BLOB)
			continue;

		hv_iterinit(lob_cols_hv);

		while( (sv = hv_iternextsv(lob_cols_hv, &p, &j)) != NULL ) {
			char sql_field[200];
			if (phs->ora_field) {	/* must match this phs by field name	*/
				char *ora_field_name = SvPV(phs->ora_field,PL_na);
				if (SvCUR(phs->ora_field) != SvCUR(sv)
					|| ibcmp(ora_field_name, SvPV(sv,PL_na), (I32)SvCUR(sv) ) )
					continue;
			}
			else {			/* basic dumb match by type		*/
				if (phs->ftype != SvIV(sv)){
					continue;
				}
				else {			/* got a type match - check it's safe	*/
					SV *sv_other;
					char *p_other;
					/* would any other lob field match this type? */
					while( (sv_other = hv_iternextsv(lob_cols_hv, &p_other, &i)) != NULL ) {
						if (phs->ftype != SvIV(sv_other))
							continue;
						if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
							PerlIO_printf(
                                DBIc_LOGPIO(imp_sth),
                                "		both %s and %s have type %d - ambiguous\n",
                                neatsvpv(sv,0), neatsvpv(sv_other,0),
                                (int)SvIV(sv_other));
						Safefree(lr);
						sv_free((SV*)lob_cols_hv);
						return oci_error(sth, errhp, OCI_ERROR,
						"Need bind_param(..., { ora_field=>... }) attribute to identify table LOB field names");
					}
				}
			}

			matched = 1;
			sprintf(sql_field, "%s%s \"%s\"",
			(SvCUR(sql_select)>7)?", ":"", p, &phs->name[1]);
			sv_catpv(sql_select, sql_field);

			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "		lob refetch %s param: otype %d, matched field '%s' %s(%s)\n",
					phs->name, phs->ftype, p,
					(phs->ora_field) ? "by name " : "by type ", sql_field);
					(void)hv_delete(lob_cols_hv, p, i, G_DISCARD);
					fbh = &lr->fbh_ary[lr->num_fields++];
					fbh->name	= phs->name;
					fbh->ftype  = phs->ftype;
					fbh->dbtype = phs->ftype;
					fbh->disize = 99;
					fbh->desc_t = OCI_DTYPE_LOB;
					OCIDescriptorAlloc_ok(imp_sth, imp_sth->envhp, &fbh->desc_h, fbh->desc_t);

			break;	/* we're done with this placeholder now	*/

		}
		if (!matched) {
			++unmatched_params;
			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
					"		lob refetch %s param: otype %d, UNMATCHED\n",
					phs->name, phs->ftype);
		}
	}
	sv_free((SV*)lob_cols_hv);

	if (unmatched_params) {
		Safefree(lr);
		return oci_error(sth, errhp, OCI_ERROR,
			"Can't match some parameters to LOB fields in the table, check type and name");
	}

	sv_catpv(sql_select, " from ");
	sv_catpv(sql_select, tablename);
	sv_catpv(sql_select, " where rowid = :rid for update"); /* get row with lock */
	if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
		PerlIO_printf(
            DBIc_LOGPIO(imp_sth),
			"		lob refetch sql: %s\n", SvPVX(sql_select));
	lr->stmthp = NULL;
	lr->bindhp = NULL;
	lr->rowid  = NULL;
	lr->parmdp_tmp = NULL;
	lr->parmdp_lob = NULL;
	OCIHandleAlloc_ok(imp_sth, imp_sth->envhp, &lr->stmthp, OCI_HTYPE_STMT, status);
	OCIStmtPrepare_log_stat(imp_sth, lr->stmthp, errhp,
		(text*)SvPVX(sql_select), SvCUR(sql_select), OCI_NTV_SYNTAX,
			OCI_DEFAULT, status);

	if (status != OCI_SUCCESS) {
		OCIHandleFree(lr->stmthp, OCI_HTYPE_STMT);
		Safefree(lr);
		return oci_error(sth, errhp, status, "OCIStmtPrepare/LOB refetch");
	}

	/* bind the rowid input */
	OCIDescriptorAlloc_ok(imp_sth, imp_sth->envhp, &lr->rowid, OCI_DTYPE_ROWID);
	OCIBindByName_log_stat(imp_sth, lr->stmthp, &lr->bindhp, errhp, (text*)":rid", 4,
		&lr->rowid, sizeof(OCIRowid*), SQLT_RDD, 0,0,0,0,0, OCI_DEFAULT, status);
	if (status != OCI_SUCCESS) {
		OCIDescriptorFree_log(imp_sth, lr->rowid, OCI_DTYPE_ROWID);
		OCIHandleFree(lr->stmthp, OCI_HTYPE_STMT);
		Safefree(lr);
		return oci_error(sth, errhp, status, "OCIBindByPos/LOB refetch");
	}

		/* define the output fields */
	for(i=0; i < lr->num_fields; ++i) {
		OCIDefine *defnp = NULL;
		imp_fbh_t *fbh = &lr->fbh_ary[i];
		phs_t *phs;
		SV **phs_svp = hv_fetch(imp_sth->all_params_hv, fbh->name,strlen(fbh->name), 0);
		if (!phs_svp)
			croak("panic: LOB refetch for '%s' param (%ld) - name not found",fbh->name,(unsigned long)i+1);
		phs = (phs_t*)(void*)SvPVX(*phs_svp);
		fbh->special = phs;
		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
				"		lob refetch %d for '%s' param: ftype %d setup\n",
		(int)i+1,fbh->name, fbh->dbtype);
		fbh->fb_ary = fb_ary_alloc(fbh->disize, 1);
		OCIDefineByPos_log_stat(imp_sth, lr->stmthp, &defnp, errhp, (ub4)i+1,
			&fbh->desc_h, -1, (ub2)fbh->ftype,
		fbh->fb_ary->aindp, 0, fbh->fb_ary->arcode, OCI_DEFAULT, status);
		if (status != OCI_SUCCESS) {
			OCIDescriptorFree_log(imp_sth, lr->rowid, OCI_DTYPE_ROWID);
			OCIHandleFree(lr->stmthp, OCI_HTYPE_STMT);
			Safefree(lr);
			fb_ary_free(fbh->fb_ary);
			fbh->fb_ary = NULL;
			return oci_error(sth, errhp, status, "OCIDefineByPos/LOB refetch");
		}
	}

	OCIHandleFree_log_stat(imp_sth, imp_sth->dschp, OCI_HTYPE_DESCRIBE, status);

	imp_sth->lob_refetch = lr;	/* structure copy */
	return 1;
}

int
post_execute_lobs(SV *sth, imp_sth_t *imp_sth, ub4 row_count)	/* XXX leaks handles on error */
{

	/* To insert a new LOB transparently (without using 'INSERT . RETURNING .')	*/
	/* we have to insert an empty LobLocator and then fetch it back from the	*/
	/* server before we can call OCILobWrite on it! This function handles that.	*/
	dTHX;
	sword status;
	int i;
	OCIError *errhp = imp_sth->errhp;
	lob_refetch_t *lr;
	D_imp_dbh_from_sth;
	SV *dbh = (SV*)DBIc_MY_H(imp_dbh);

	if (!imp_sth->auto_lob)
		 return 1;	/* application doesn't want magical lob handling */

	if (imp_sth->stmt_type == OCI_STMT_BEGIN || imp_sth->stmt_type == OCI_STMT_DECLARE){
	/* PL/SQL is handled by lob_phs_ora_free_templobpost_execute */
		if (imp_sth->has_lobs) { 	  /*get rid of OCILob Temporary used in non inout bind*/
			SV *phs_svp;
			I32 i;
			char *p;
			hv_iterinit(imp_sth->all_params_hv);
			while( (phs_svp = hv_iternextsv(imp_sth->all_params_hv, &p, &i)) != NULL ) {
				phs_t *phs = (phs_t*)(void*)SvPVX(phs_svp);



				if (phs->desc_h && !phs->is_inout){
                    OCILobFreeTemporary_log_stat(imp_sth, imp_sth->svchp, imp_sth->errhp, phs->desc_h, status);


				/*	boolean lobEmpty=1;*/
				/*	OCIAttrSet_log_stat(phs->desc_h, phs->desc_t,&lobEmpty, 0, OCI_ATTR_LOBEMPTY, imp_sth->errhp, status);*/
				/*	OCIHandleFree_log_stat(phs->desc_h, phs->desc_t, status);*/
				}
				/*this seem to cause an error later on so I just got rid of it for Now does */
				/* not seem to kill anything */
			}
		}
		return 1;
	}

	if (row_count == 0)
		return 1;	/* nothing to do */
	if (row_count  > 1)
		return oci_error(sth, errhp, OCI_ERROR, "LOB refetch attempted for multiple rows");

	if (!imp_sth->lob_refetch) {
		if (!init_lob_refetch(sth, imp_sth))
			return 0;	/* init_lob_refetch already called oci_error */
	}
	lr = imp_sth->lob_refetch;

	OCIAttrGet_stmhp_stat(imp_sth, lr->rowid, 0, OCI_ATTR_ROWID,status);

	if (status != OCI_SUCCESS)
		return oci_error(sth, errhp, status, "OCIAttrGet OCI_ATTR_ROWID /LOB refetch");

	OCIStmtExecute_log_stat(imp_sth, imp_sth->svchp, lr->stmthp, errhp,1, 0, NULL, NULL, OCI_DEFAULT, status);	/* execute and fetch */

	if (status != OCI_SUCCESS)
		return oci_error(sth, errhp, status,

	ora_sql_error(imp_sth,"OCIStmtExecute/LOB refetch"));

	for(i=0; i < lr->num_fields; ++i) {
		imp_fbh_t *fbh = &lr->fbh_ary[i];
		int rc = fbh->fb_ary->arcode[0];
		phs_t *phs = (phs_t*)fbh->special;
		ub4 amtp;

		(void)SvUPGRADE(phs->sv, SVt_PV);

		amtp = SvCUR(phs->sv);		/* XXX UTF8? */
		if (rc == 1405) {		/* NULL - return undef */
			sv_set_undef(phs->sv);
			status = OCI_SUCCESS;
		}
		else if (amtp > 0) {	/* since amtp==0 & OCI_ONE_PIECE fail (OCI 8.0.4) */
			if( ! fbh->csid ) {
				ub1 csform = SQLCS_IMPLICIT;
				ub2 csid = 0;
				OCILobCharSetForm_log_stat(imp_sth,
                                           imp_sth->envhp,
                                           errhp,
                                           (OCILobLocator*)fbh->desc_h,
                                           &csform,
                                           status );
				if (status != OCI_SUCCESS)
					return oci_error(sth, errhp, status, "OCILobCharSetForm");
#ifdef OCI_ATTR_CHARSET_ID
		/* Effectively only used so AL32UTF8 works properly */
				OCILobCharSetId_log_stat(imp_sth,
                                         imp_sth->envhp,
                                         errhp,
                                         (OCILobLocator*)fbh->desc_h,
                                         &csid,
                                         status );
				if (status != OCI_SUCCESS)
					return oci_error(sth, errhp, status, "OCILobCharSetId");
#endif /* OCI_ATTR_CHARSET_ID */
		/* if data is utf8 but charset isn't then switch to utf8 csid */
				csid = (SvUTF8(phs->sv) && !CS_IS_UTF8(csid)) ? utf8_csid : CSFORM_IMPLIED_CSID(imp_dbh, csform);
				fbh->csid = csid;
				fbh->csform = csform;
			}

			if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
				PerlIO_printf(
                    DBIc_LOGPIO(imp_sth),
                    "	  calling OCILobWrite fbh->csid=%d fbh->csform=%d amtp=%d\n",
					fbh->csid, fbh->csform, amtp );

			OCILobWrite_log_stat(imp_sth, imp_sth->svchp, errhp,
				(OCILobLocator*)fbh->desc_h, &amtp, 1, SvPVX(phs->sv), amtp, OCI_ONE_PIECE,
				0,0, fbh->csid ,fbh->csform, status);

			if (status != OCI_SUCCESS) {
				return oci_error(sth, errhp, status, "OCILobWrite in post_execute_lobs");
			}

		} else {			/* amtp==0 so truncate LOB to zero length */
			OCILobTrim_log_stat(imp_sth, imp_sth->svchp, errhp, (OCILobLocator*)fbh->desc_h, 0, status);

			if (status != OCI_SUCCESS) {
				return oci_error(sth, errhp, status, "OCILobTrim in post_execute_lobs");
			}

		}

		if (DBIc_DBISTATE(imp_sth)->debug >= 3 || dbd_verbose >= 3 )
			PerlIO_printf(
                DBIc_LOGPIO(imp_sth),
			"		lob refetch %d for '%s' param: ftype %d, len %ld: %s %s\n",
			i+1,fbh->name, fbh->dbtype, ul_t(amtp),
			(rc==1405 ? "NULL" : (amtp > 0) ? "LobWrite" : "LobTrim"), oci_status_name(status));

		if (status != OCI_SUCCESS) {
			return oci_error(sth, errhp, status, "OCILobTrim/OCILobWrite/LOB refetch");
		}
	}

	if (DBIc_has(imp_dbh,DBIcf_AutoCommit))
		dbd_db_commit(dbh, imp_dbh);

	return 1;
}

void
ora_free_lob_refetch(SV *sth, imp_sth_t *imp_sth)
{
	dTHX;
	lob_refetch_t *lr = imp_sth->lob_refetch;
	int i;
	sword status;
	if (lr->rowid)
		OCIDescriptorFree_log(imp_sth, lr->rowid, OCI_DTYPE_ROWID);
	OCIHandleFree_log_stat(imp_sth, lr->stmthp, OCI_HTYPE_STMT, status);

	if (status != OCI_SUCCESS)
		oci_error(sth, imp_sth->errhp, status, "ora_free_lob_refetch/OCIHandleFree");

	for(i=0; i < lr->num_fields; ++i) {
		imp_fbh_t *fbh = &lr->fbh_ary[i];
		ora_free_fbh_contents(sth, fbh);
	}
	sv_free(lr->fbh_ary_sv);
	Safefree(imp_sth->lob_refetch);
	imp_sth->lob_refetch = NULL;
}

ub4
ora_db_version(SV *dbh, imp_dbh_t *imp_dbh)
{
	dTHX;
	sword status;
	text buf[2];
	ub4 vernum;

	if( imp_dbh->server_version > 0 ) {
		return imp_dbh->server_version;
	}


	/* XXX should possibly create new session before ending the old so	*/
	/* that if the new one can't be created, the old will still work.	*/
	OCIServerRelease_log_stat(imp_dbh, imp_dbh->svchp, imp_dbh->errhp, buf, 2,OCI_HTYPE_SVCCTX, &vernum , status);
	if (status != OCI_SUCCESS) {
		oci_error(dbh, imp_dbh->errhp, status, "OCISessionServerRelease");
		return 0;
	}
	imp_dbh->server_version = vernum;
	return vernum;
}
