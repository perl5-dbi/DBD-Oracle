/* notes {{{
 * This module attempts to provide reuse of OCIEnv
 * and storage for session pool(s).
 *
 * Note, configuration uses acronym "drcp", which is
 * somewhat misleading. DRCP stands for "connection pooling"
 * and DBD::Oracle offers "session pooling", which is
 * not "connection pooling" (that is also offered by Oracle).
 *
 * To make things complex DBD::Oracle offers 2 ways to
 * share connections between threads. Such sharing
 * requires that one thread does not use pointers
 * to data allocated in another thread. In perl,
 * each thread gets copy of perl interpreter, and each
 * thread has its own heap for memory allocation.
 * When the thread ends, that memory may get released,
 * which leads to problems if the memory is referenced
 * from another thread.
 *
 * It appears, that OCI uses itsown memory pool for
 * allocation, so the data allocated in one thread
 * is still accessible in another thread. At least
 * tests don't fail in such cases. There are
 * also OCI internal mutexes that protect access to
 * that memory. The good news, one may ask OCI to allocate
 * some memory for the caller. That is what this
 * module is using.
 *
 * When OCIEnv is created, it uses 3 parameters
 * that define the functionality of new OCIEnv.
 * Those are: mode, charset and ncharset. So, when
 * we need some OCIEnv we have to check for existing
 * ones that have matching parameters. If charset
 * or ncharset is 0, then current value from environment
 * is used (OCINlsEnvironmentVariableGet)
 *
 * The session pool is identified (in addition to
 * charsets and mode for OCIEnv) by dbname, user
 * and "rlb" (activity of load-balancer). Since all
 * sessions are "homogenous", Oracle won't check
 * password when new session is requested. So probably
 * it makes sense to check it here. So password
 * is also saved.
 *
 * This module does not use HV to find cached
 * handles because HV may need to allocate memory
 * from current thread, and that is not good. Still
 * it is very unlikely that someone uses thousands
 * of different pools/characters, so simple
 * running through doubly-linked list should be
 * fast enough (and maybe even faster) than hashing.
 *
 * Now, how long shall be cached all handles? As long
 * as there are users. Each open connection is a user.
 * Additionally every "driver"-object is a user.
 * The drivier-object is allocated in each thread
 * that tries to connect to DB and is released when
 * thread ends.
 }}} */
#include "Oracle.h"
DBISTATE_DECLARE;

typedef struct llist_t llist_t;
/* small implementation for doubly-linked list {{{ */
struct llist_t{
    llist_t * left;
    llist_t * right;
};

#define llist_empty(list) ((list)->left == (list))

#define llist_init(lst) do{\
    llist_t * list = lst;\
    list->right = list->left = list;\
}while(0)

#define llist_add(aleft, aright) do{\
    llist_t * old;\
    llist_t * left = aleft;\
    llist_t * right = aright;\
    old = left->right;\
    left->right = right;\
    old->left = right->left;\
    right->left = left;\
    old->left->right = old;\
}while(0)

#define llist_drop(ael) do{\
    llist_t * el = ael;\
    if(!llist_empty(el)) {\
        el->left->right = el->right;\
        el->right->left = el->left;\
        llist_init(el);\
    } \
}while(0)

// this is pointer to the left element in chain
#define llist_left(list) (list)->left

// this one is a pointer to the right element in chain
#define llist_right(list) (list)->right
/* }}} */

#if defined(USE_ITHREADS)
static perl_mutex mng_lock;
#endif
static llist_t mng_list;
static int dr_instances;
/* to get information about charsets we need these */
static OCIEnv * mng_env;
static OCIError * mng_err;

/*dbd_dr_globals_init{{{*/
void
dbd_dr_globals_init()
{
#if defined(USE_ITHREADS)
    dTHX;
    MUTEX_INIT(&mng_lock);
#endif
    llist_init(&mng_list);
    dr_instances = 0;
    mng_env = NULL;
    mng_err = NULL;
}
/*}}}*/

struct box_st{
    llist_t lock;
    int refs; /* this shall be positiv for OCIEnv and negativ for Session Pool */
};

struct env_box_st
{
    box_t base;
    OCIEnv * envhp;
    ub4 mode;
    ub2 cset;
    ub2 ncset;
};
typedef struct env_box_st env_box_t;

#ifdef ORA_OCI_112
struct pool_box_st{
    box_t base;
    env_box_t * env;
    OCISPool *poolhp;
    OCIError *errhp;
    OraText *name;
    ub4	name_len;
    ub2 pass_len;
    ub2 dbname_len;
    /* all text strings are stored below. The buffer
     * is long enough to contain dbname_len+user_len+pass_len + 4 bytes.
     * So pass is box->buf + 1;
     * dbname is box->buf + 2 + box->pass_len
     * user is box->buf + 3 + box->dbname_len + box->pass_len
     * First byte is 0 if RLB is not desired.
     */
    char buf[1];
};
typedef struct pool_box_st pool_box_t;
#endif

#define tracer(imp, dlvl, vlvl, ...) if \
    (DBIc_DBISTATE(imp)->debug >= (dlvl) || dbd_verbose >= (vlvl) )\
            PerlIO_printf(DBIc_LOGPIO(imp), __VA_ARGS__)

/* local_error{{{*/
static int
local_error(pTHX_ SV * h, const char * fmt, ...)
{
    va_list ap;
    SV * txt_sv = sv_newmortal();
    SV * code_sv = get_sv("DBI::stderr", 0);
    D_imp_xxh(h);
    if(code_sv == NULL)
    {
        code_sv  = sv_newmortal();
        sv_setiv(code_sv, 2000000000);
    }
    va_start(ap, fmt);
    sv_vsetpvf(txt_sv, fmt, &ap);
    va_end(ap);
    DBIh_SET_ERR_SV(h, imp_xxh, code_sv, txt_sv, &PL_sv_undef, &PL_sv_undef);
    return FALSE;
}
/*}}}*/
/* release_env{{{*/
static void
release_env(pTHX_ env_box_t * box)
{
    llist_drop(&box->base.lock);
    if(dbd_verbose >= 3)
        warn("Releasing OCIEnv %p\n", box->envhp);
    (void)OCIHandleFree(box->envhp, OCI_HTYPE_ENV);
}/*}}}*/

/* new_envhp_box{{{*/
static int
new_envhp_box(pTHX_ env_box_t ** slot, dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    env_box_t * box;
    OCIEnv * envhp;
    sword status = OCIEnvNlsCreate(
            &envhp, ctrl->mode,
            0, NULL, NULL, NULL,
            sizeof(*box), (dvoid**)&box,
            imp_dbh->cset, imp_dbh->ncset
    );
    if (status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCIEnv");
    tracer(imp_dbh, 3, 3, "allocated new OCIEnv %p (cset %d, ncset %d, mode %d)\n",
            envhp, (int)imp_dbh->cset, (int)imp_dbh->ncset, (int)ctrl->mode);
    llist_init(&box->base.lock);
    box->envhp = envhp;
    box->base.refs = dr_instances;
    box->cset = imp_dbh->cset;
    box->ncset = imp_dbh->ncset;
    box->mode = ctrl->mode;
    llist_add(&mng_list, &box->base.lock);
    *slot = box;
    return TRUE;
}/*}}}*/
/* figure_out_charsets {{{*/
static int
figure_out_charsets(pTHX_ dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    if(ctrl->cset != NULL)
    {
        imp_dbh->cset = OCINlsCharSetNameToId(
                mng_env, (OraText*)ctrl->cset
        );
        if(imp_dbh->cset == 0) return local_error(
                aTHX_ ctrl->dbh, "Invalid ora_charset '%s'", ctrl->cset
        );
    }
    else if(imp_dbh->cset == 0)
    {
        size_t rsize;
        sword status = OCINlsEnvironmentVariableGet(
            &imp_dbh->cset, 0, OCI_NLS_CHARSET_ID, 0, &rsize
        );
        if(status != OCI_SUCCESS || imp_dbh->cset == 0)
            return local_error(aTHX_ ctrl->dbh, "NLS_LANG appears to be invalid");
    }
    if(ctrl->ncset != NULL)
    {
        imp_dbh->ncset = OCINlsCharSetNameToId(
                mng_env, (OraText*)ctrl->ncset
        );
        if(imp_dbh->ncset == 0) return local_error(
                aTHX_ ctrl->dbh, "Invalid ora_ncharset '%s'", ctrl->ncset
        );
    }
    else if(imp_dbh->ncset == 0)
    {
        size_t rsize;
        sword status = OCINlsEnvironmentVariableGet(
            &imp_dbh->ncset, 0, OCI_NLS_NCHARSET_ID, 0, &rsize
        );
        if(status != OCI_SUCCESS || imp_dbh->ncset == 0)
            return local_error(aTHX_ ctrl->dbh, "NLS_NCHAR appears to be invalid");
    }

    return TRUE;
}/*}}}*/
/*find_env{{{*/
static env_box_t *
find_env(ub4 mode, ub2 cset, ub2 ncset)
{
    llist_t * base = llist_left(&mng_list);
    while(base != &mng_list)
    {
        env_box_t * box = (env_box_t *)base;
        if(box->base.refs > 0
                && box->mode == mode
                && box->ncset == ncset
                && box->cset == cset
          )
        {
            /* check if this handle is still valid */
            if(box->base.refs == dr_instances)
            {
                OCIError 	*errhp;
                sword status=OCIHandleAlloc(
                        box->envhp,
                        (dvoid**)&errhp,
                        OCI_HTYPE_ERROR, 0, NULL
                );
                if(status != OCI_SUCCESS)
                {
                    llist_drop(base);
                    OCIHandleFree(box->envhp, OCI_HTYPE_ENV);
                    return NULL;
                }
            }
            return box;
        }
        base = llist_left(base);
    }
    return NULL;
}
/*}}}*/
/* simple_connect {{{*/
static int
simple_connect(pTHX_ dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    sword status;
    ub4 ulen, plen, ctype;

    tracer(imp_dbh, 3, 3, "using OCIEnv %p to connect\n", imp_dbh->envhp);

    OCIHandleAlloc_ok(
            imp_dbh, imp_dbh->envhp, &imp_dbh->errhp,
            OCI_HTYPE_ERROR, status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCIError\n");

    OCIHandleAlloc_ok(
            imp_dbh, imp_dbh->envhp, &imp_dbh->srvhp,
            OCI_HTYPE_SERVER, status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCIServer\n");

    OCIHandleAlloc_ok(
            imp_dbh, imp_dbh->envhp, &imp_dbh->svchp,
            OCI_HTYPE_SVCCTX, status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCISvcCtx\n");

    OCIHandleAlloc_ok(
            imp_dbh, imp_dbh->envhp, &imp_dbh->seshp,
            OCI_HTYPE_SESSION, status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCISession\n");

    OCIServerAttach_log_stat(imp_dbh, ctrl->dbname,OCI_DEFAULT, status);
    if (status != OCI_SUCCESS)
            return oci_error(ctrl->dbh, imp_dbh->errhp, status, "OCIServerAttach");

    OCIAttrSet_log_stat(
            imp_dbh, imp_dbh->svchp,
            OCI_HTYPE_SVCCTX,
            imp_dbh->srvhp,
            (ub4) 0, OCI_ATTR_SERVER, imp_dbh->errhp, status
    );
    if (status != OCI_SUCCESS) return oci_error(
            ctrl->dbh, imp_dbh->errhp, status, "OCIAttrSet OCI_HTYPE_SVCCTX"
    );
    plen = (ub4)strlen(ctrl->pwd);
    ulen = (ub4)strlen(ctrl->uid);
    if(plen == 0 && ulen == 0) ctype = OCI_CRED_EXT;
    else
    {
        ctype = OCI_CRED_RDBMS;
        OCIAttrSet_log_stat(
                imp_dbh, imp_dbh->seshp,
                OCI_HTYPE_SESSION,
                ctrl->uid, ulen,
                (ub4) OCI_ATTR_USERNAME,
                imp_dbh->errhp, status
        );
        if (status != OCI_SUCCESS) return oci_error(
                ctrl->dbh, imp_dbh->errhp, status, "OCIAttrSet OCI_ATTR_USERNAME"
        );

        OCIAttrSet_log_stat(
                imp_dbh, imp_dbh->seshp,
                OCI_HTYPE_SESSION,
                ((plen) ? ctrl->pwd : NULL), plen,
                (ub4) OCI_ATTR_PASSWORD,
                imp_dbh->errhp, status
        );
        if (status != OCI_SUCCESS) return oci_error(
                ctrl->dbh, imp_dbh->errhp, status, "OCIAttrSet OCI_ATTR_PASSWORD"
        );
    }
#ifdef ORA_OCI_112
    if(ctrl->driver_name_len != 0)
    {
        OCIAttrSet_log_stat(
            imp_dbh, imp_dbh->seshp, OCI_HTYPE_SESSION,
            ctrl->driver_name, ctrl->driver_name_len,
            OCI_ATTR_DRIVER_NAME,
            imp_dbh->errhp, status
        );
        if (status != OCI_SUCCESS) return oci_error(
                ctrl->dbh, imp_dbh->errhp, status, "OCIAttrSet OCI_ATTR_DRIVER_NAME"
        );
    }
#endif

    OCISessionBegin_log_stat(
            imp_dbh, imp_dbh->svchp, imp_dbh->errhp, imp_dbh->seshp,
            ctype, ctrl->session_mode, status
    );
    if (status == OCI_SUCCESS_WITH_INFO)
    {
        /* eg ORA-28011: the account will expire soon; change your password now */
        oci_error(ctrl->dbh, imp_dbh->errhp, status, "OCISessionBegin");
        status = OCI_SUCCESS;
    }
    if (status != OCI_SUCCESS)
        return oci_error(ctrl->dbh, imp_dbh->errhp, status, "OCISessionBegin");

    OCIAttrSet_log_stat(
            imp_dbh, imp_dbh->svchp,
            (ub4) OCI_HTYPE_SVCCTX,
            imp_dbh->seshp, (ub4) 0,
            (ub4) OCI_ATTR_SESSION,
            imp_dbh->errhp, status
    );
    if (status != OCI_SUCCESS) return oci_error(
            ctrl->dbh, imp_dbh->errhp, status, "OCIAttrSet OCI_ATTR_SESSION"
    );
    return TRUE;
}/*}}}*/
#ifdef ORA_OCI_112
/*release_pool{{{*/
static void
release_pool(pTHX_ pool_box_t * box)
{
    env_box_t * ebox;
    if(dbd_verbose >= 3)
        warn("Releasing pool %p\n", box->poolhp);

    ebox = box->env;
    llist_drop(&box->base.lock);
    (void)OCISessionPoolDestroy (box->poolhp, box->errhp, OCI_DEFAULT);
    (void)OCIHandleFree(box->poolhp, OCI_HTYPE_SPOOL);
    ebox->base.refs--;
    if(ebox->base.refs == 0) release_env(aTHX_ ebox);
}/*}}}*/
/*cnx_pool_min{{{*/
void
cnx_pool_min(pTHX_ SV * dbh, imp_dbh_t * imp_dbh, ub4 val)
{
    pool_box_t * box;
    sword status;
    if(imp_dbh->lock->refs > 0) return;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrSet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&val, sizeof(val),
            OCI_ATTR_SPOOL_MIN, box->errhp
    );
    if(status != OCI_SUCCESS)
        (void)oci_error(dbh, box->errhp, status, "OCIAttrSet POOL_MIN");
}/*}}}*/
/*cnx_pool_max{{{*/
void
cnx_pool_max(pTHX_ SV * dbh, imp_dbh_t * imp_dbh, ub4 val)
{
    pool_box_t * box;
    sword status;
    if(imp_dbh->lock->refs > 0) return;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrSet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&val, sizeof(val),
            OCI_ATTR_SPOOL_MAX, box->errhp
    );
    if(status != OCI_SUCCESS)
        (void)oci_error(dbh, box->errhp, status, "OCIAttrSet POOL_MAX");
}/*}}}*/
/*cnx_pool_incr{{{*/
void
cnx_pool_incr(pTHX_ SV * dbh, imp_dbh_t * imp_dbh, ub4 val)
{
    pool_box_t * box;
    sword status;
    if(imp_dbh->lock->refs > 0) return;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrSet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&val, sizeof(val),
            OCI_ATTR_SPOOL_INCR, box->errhp
    );
    if(status != OCI_SUCCESS)
        (void)oci_error(dbh, box->errhp, status, "OCIAttrSet POOL_INCR");
}/*}}}*/
/*cnx_pool_mode{{{*/
void
cnx_pool_mode(pTHX_ SV * dbh, imp_dbh_t * imp_dbh, ub4 val)
{
    pool_box_t * box;
    sword status;
    if(imp_dbh->lock->refs > 0) return;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrSet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&val, sizeof(val),
            OCI_ATTR_SPOOL_GETMODE, box->errhp
    );
    if(status != OCI_SUCCESS)
        (void)oci_error(dbh, box->errhp, status, "OCIAttrSet POOL_GETMODE");
}/*}}}*/
/*cnx_pool_wait{{{*/
#if OCI_MAJOR_VERSION > 18
void
cnx_pool_wait(pTHX_ SV * dbh, imp_dbh_t * imp_dbh, ub4 val)
{
    pool_box_t * box;
    sword status;
    if(imp_dbh->lock->refs > 0) return;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrSet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&val, sizeof(val),
            OCI_ATTR_SPOOL_WAIT_TIMEOUT, box->errhp
    );
    if(status != OCI_SUCCESS)
        (void)oci_error(dbh, box->errhp, status, "OCIAttrSet POOL_WAIT_TIMEOUT");
}
#endif
/*}}}*/
/*cnx_is_pooled_session{{{*/
int
cnx_is_pooled_session(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    return (imp_dbh->lock->refs < 0);
}/*}}}*/
/*cnx_get_pool_mode{{{*/
int
cnx_get_pool_mode(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_GETMODE, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_METHOD");
    return 0;
}/*}}}*/
/*cnx_get_pool_wait{{{*/
#if OCI_MAJOR_VERSION > 18
int
cnx_get_pool_wait(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_WAIT_TIMEOUT, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_WAIT_TIMEOUT");
    return 0;
}
#endif
/*}}}*/
/*cnx_get_pool_used{{{*/
int
cnx_get_pool_used(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_BUSY_COUNT, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_USED");
    return 0;
}/*}}}*/
/*cnx_get_pool_max{{{*/
int
cnx_get_pool_max(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_MAX, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_MAX");
    return 0;
}/*}}}*/
/*cnx_get_pool_min{{{*/
int
cnx_get_pool_min(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_MIN, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_MIN");
    return 0;
}/*}}}*/
/*cnx_get_pool_incr{{{*/
int
cnx_get_pool_incr(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    pool_box_t * box;
    ub4 v, l;
    sword status;
    if(imp_dbh->lock->refs > 0) return 0;
    box = ((pool_box_t*)imp_dbh->lock);
    status = OCIAttrGet(
            box->poolhp, OCI_HTYPE_SPOOL,
            (dvoid*)&v, &l,
            OCI_ATTR_SPOOL_INCR, box->errhp
    );
    if(status == OCI_SUCCESS) return (int)v;
    (void)oci_error(dbh, box->errhp, status, "OCIAttrGet POOL_INCR");
    return 0;
}/*}}}*/
/*cnx_get_pool_rlb{{{*/
int
cnx_get_pool_rlb(pTHX_ SV *dbh, imp_dbh_t * imp_dbh)
{
    if(imp_dbh->lock->refs > 0) return 0;
    return (int)(((pool_box_t*)imp_dbh->lock)->buf[0]);
}/*}}}*/
/*find_pool{{{*/
static pool_box_t *
find_pool(dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    llist_t * base = llist_left(&mng_list);
    while(base != &mng_list)
    {
        char * name;
        pool_box_t * box = (pool_box_t *)base;
        base = llist_left(base);
        if(box->base.refs > 0) continue;
        if(box->env->mode != ctrl->mode) continue;
        if(box->env->ncset != imp_dbh->ncset) continue;
        if(box->env->cset != imp_dbh->cset) continue;
        name = box->buf + 2 + box->pass_len;
        if(0 != strcmp(name, ctrl->dbname)) continue;
        name += box->dbname_len + 1;
        if(0 != strcmp(name, ctrl->uid)) continue;
        if((int)(box->buf[0]) != (int)(ctrl->pool_rlb)) continue;
        return box;
    }
    return NULL;
}
/*}}}*/
/* new_pool_box{{{*/
int
new_pool_box(pTHX_ pool_box_t ** slot, dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    OCIEnv * envhp = imp_dbh->envhp;
    pool_box_t * box;
    char * name;
    OCISPool *poolhp;
    ub4 mode;
    ub4 dbname_len = strlen(ctrl->dbname);
    ub4 user_len = strlen(ctrl->uid);
    ub4 pwd_len = strlen(ctrl->pwd);
    size_t boxlen = sizeof(*box) + dbname_len + user_len + pwd_len + 3;

    sword status = OCIHandleAlloc(
            envhp, (dvoid*)&poolhp, OCI_HTYPE_SPOOL, boxlen, (dvoid**)&box
    );
    if (status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCISPool");

    status = OCIHandleAlloc(envhp, (dvoid*)&box->errhp, OCI_HTYPE_ERROR, 0, NULL);
    if (status != OCI_SUCCESS)
    {
        OCIHandleFree(poolhp, OCI_HTYPE_SPOOL);
        return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCIError");
    }
#ifdef ORA_OCI_112
    if(ctrl->driver_name_len != 0)
    {
        OCIAuthInfo * authp;
        status = OCIHandleAlloc(envhp, (dvoid*)&authp, OCI_HTYPE_AUTHINFO, 0, NULL);
        if(status != OCI_SUCCESS)
        {
            OCIHandleFree(box->errhp, OCI_HTYPE_ERROR);
            OCIHandleFree(poolhp, OCI_HTYPE_SPOOL);
            return local_error(aTHX_ ctrl->dbh, "Failed to allocate OCIAuthInfo");
        }
        status = OCIAttrSet(
            authp, OCI_HTYPE_AUTHINFO,
            (OraText*)ctrl->driver_name, ctrl->driver_name_len,
            OCI_ATTR_DRIVER_NAME,
            box->errhp
        );
        if(status != OCI_SUCCESS)
        {
            (void)oci_error(ctrl->dbh, box->errhp, status, "OCIAttrSet DriverName");
            OCIHandleFree(box->errhp, OCI_HTYPE_ERROR);
            OCIHandleFree(authp, OCI_HTYPE_AUTHINFO);
            OCIHandleFree(poolhp, OCI_HTYPE_SPOOL);
            return FALSE;
        }
        status = OCIAttrSet(
            poolhp, OCI_HTYPE_SPOOL,
            authp, 0,
            OCI_ATTR_SPOOL_AUTH,
            box->errhp
        );
        if(status != OCI_SUCCESS)
        {
            (void)oci_error(ctrl->dbh, box->errhp, status, "OCIAttrSet OCIAuthInfo");
            OCIHandleFree(box->errhp, OCI_HTYPE_ERROR);
            OCIHandleFree(authp, OCI_HTYPE_AUTHINFO);
            OCIHandleFree(poolhp, OCI_HTYPE_SPOOL);
            return FALSE;
        }
        OCIHandleFree(authp, OCI_HTYPE_AUTHINFO);
    }
#endif


    mode = OCI_SPC_HOMOGENEOUS;
    if(!ctrl->pool_rlb) mode |= OCI_SPC_NO_RLB;
    status = OCISessionPoolCreate(
            envhp,
            box->errhp,
            poolhp,
            &box->name,
            &box->name_len,
            (OraText *) ctrl->dbname,
            dbname_len,
            ctrl->pool_min,
            ctrl->pool_max,
            ctrl->pool_incr,
            (OraText *) ctrl->uid,
            user_len,
            (OraText *) ctrl->pwd,
            pwd_len,
            mode
    );
    if (status != OCI_SUCCESS)
    {
        (void)oci_error(ctrl->dbh, box->errhp, status, "OCISessionPoolCreate");
        OCIHandleFree(box->errhp, OCI_HTYPE_ERROR);
        OCIHandleFree(poolhp, OCI_HTYPE_SPOOL);
        return FALSE;
    }
    llist_init(&box->base.lock);
    box->env = (env_box_t *)imp_dbh->lock;
    box->buf[0] = ctrl->pool_rlb;
    name = box->buf + 1;
    strcpy(name, ctrl->pwd);
    box->pass_len = pwd_len;
    name += pwd_len + 1;
    strcpy(name, ctrl->dbname);
    box->dbname_len = dbname_len;
    name += dbname_len + 1;
    strcpy(name, ctrl->uid);

    box->base.refs = -dr_instances;
    llist_add(&mng_list, &box->base.lock);
    box->poolhp = poolhp;

    *slot = box;
    return TRUE;
}/*}}}*/
/* session_from_pool {{{*/
static int
session_from_pool(pTHX_ dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    sword status;
    OCIAuthInfo *authp;
    OraText *rettag;
    OraText *tag;
    ub4 rettagl;
    STRLEN tagl;
    boolean session_tag_found;
    /* try to find existing pool */
    pool_box_t * box = find_pool(ctrl);
    if(box != NULL)
    {
        if(0 != strcmp(box->buf + 1, ctrl->pwd))
            return local_error(aTHX_ ctrl->dbh, "Password for session is wrong");
    }
    else if(!new_pool_box(aTHX_ &box, ctrl)) return FALSE;

    /* replace env-box with pool-box. It shall be cleared by DESTROY */
    imp_dbh->lock = &box->base;
    box->base.refs--; /* refcount for pool is negative! I know, I know... */

    OCIHandleAlloc_ok(
        imp_dbh, imp_dbh->envhp, &imp_dbh->errhp, OCI_HTYPE_ERROR,  status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "OCIError alloc failed");

    OCIHandleAlloc_ok(
        imp_dbh, imp_dbh->envhp, &authp, OCI_HTYPE_AUTHINFO, status
    );
    if(status != OCI_SUCCESS)
        return local_error(aTHX_ ctrl->dbh, "OCIAuthInfo alloc failed");
    if(ctrl->pool_class != NULL)
    {
        tag = (OraText*)SvPV(ctrl->pool_class, tagl);
        OCIAttrSet_log_stat(
                imp_dbh, authp,
                OCI_HTYPE_AUTHINFO,
                tag, (ub4) tagl,
                OCI_ATTR_CONNECTION_CLASS, imp_dbh->errhp, status
        );
        if(status != OCI_SUCCESS) return local_error(aTHX_ ctrl->dbh,
                "OCIAuthInfo setting connection class failed");
    }

    tag = NULL;
    tagl = 0;
    if(ctrl->pool_tag != NULL)
    {
        tag = (OraText*)SvPV(ctrl->pool_tag, tagl);
        if(tagl == 0) tag = NULL;
    }

    OCISessionGet_log_stat(
            imp_dbh, imp_dbh->envhp,
            imp_dbh->errhp,
            &imp_dbh->svchp,
            authp,
            box->name, box->name_len,
            tag, (ub4)tagl,
            &rettag, &rettagl, &session_tag_found,
            status
    );
    if (status != OCI_SUCCESS)
    {
        (void)oci_error(ctrl->dbh, imp_dbh->errhp, status, "OCISessionGet");
        OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);
        return FALSE;
    }
    if(session_tag_found && rettagl != 0)
    {
        if(imp_dbh->session_tag != NULL) SvREFCNT_dec(imp_dbh->session_tag);
        imp_dbh->session_tag = newSVpv((char*)rettag, rettagl);
    }

    OCIHandleFree_log_stat(imp_dbh, authp, OCI_HTYPE_AUTHINFO, status);

    /* Get server and session handles from service context handle,
     * allocated by OCISessionGet. */
    OCIAttrGet_log_stat(
            imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, &imp_dbh->srvhp, NULL,
            OCI_ATTR_SERVER, imp_dbh->errhp, status
    );
    if(status != OCI_SUCCESS)
        return oci_error(ctrl->dbh, imp_dbh->errhp, status, "Server get");
    OCIAttrGet_log_stat(
            imp_dbh, imp_dbh->svchp, OCI_HTYPE_SVCCTX, &imp_dbh->seshp, NULL,
            OCI_ATTR_SESSION, imp_dbh->errhp, status
    );
    if(status != OCI_SUCCESS)
        return oci_error(ctrl->dbh, imp_dbh->errhp, status, "Session get");

    return 1;
}
/*}}}*/
#endif

#if defined(USE_ITHREADS)
static int _cnx_establish
#else
int cnx_establish
#endif
(pTHX_ dblogin_info_t * ctrl)
{
    imp_dbh_t * imp_dbh = ctrl->imp_dbh;
    env_box_t * env_box;

    if(!figure_out_charsets(aTHX_ ctrl)) return FALSE;
    /* try to find existing OCIEnv */
    env_box = find_env(ctrl->mode, imp_dbh->cset, imp_dbh->ncset);
    if(env_box == NULL && !new_envhp_box(aTHX_ &env_box, ctrl)) return FALSE;

    env_box->base.refs++;
    imp_dbh->envhp = env_box->envhp;
    imp_dbh->lock = &env_box->base;
    imp_dbh->errhp = NULL;
    imp_dbh->srvhp = NULL;
    imp_dbh->svchp = NULL;
    imp_dbh->seshp = NULL;
    /* Now I want DESTROY to be called if something goes wrong */
    DBIc_IMPSET_on(imp_dbh);
#ifdef ORA_OCI_112
    if(ctrl->pool_max != 0)
        return session_from_pool(aTHX_ ctrl);
#endif
    return simple_connect(aTHX_ ctrl);
}

#if defined(USE_ITHREADS)
int
cnx_establish(pTHX_ dblogin_info_t * ctrl)
{
    int rv;
    MUTEX_LOCK(&mng_lock);
    rv = _cnx_establish(aTHX_ ctrl);
    MUTEX_UNLOCK(&mng_lock);
    return rv;
}
#endif

/*dbd_dr_mng{{{
 * this function is called every time new DR object is created.
 * It is responsible for incrementing refs on all cached objects
 */
void
dbd_dr_mng()
{
    llist_t * el;
#if defined(USE_ITHREADS)
    dTHX;
    MUTEX_LOCK(&mng_lock);
#endif
    dr_instances++;
    el = llist_left(&mng_list);
    while(el != &mng_list)
    {
        box_t * base = (box_t *)el;
#ifdef ORA_OCI_112
        if(base->refs < 0) base->refs--;
        else
#endif
        base->refs++;
        el = llist_left(el);
    }
    if(mng_env == NULL)
    {
        sword status = OCIEnvNlsCreate(
                &mng_env, OCI_DEFAULT,
                0, NULL, NULL, NULL,
                0, NULL, 0, 0
        );
        if(status == OCI_SUCCESS)
            status=OCIHandleAlloc(
                    mng_env,
                    (dvoid**)&mng_err,
                    OCI_HTYPE_ERROR,
                    0, NULL
            );
        utf8_csid	   = OCINlsCharSetNameToId(mng_env, (void*)"UTF8");
        al32utf8_csid  = OCINlsCharSetNameToId(mng_env, (void*)"AL32UTF8");
        al16utf16_csid = OCINlsCharSetNameToId(mng_env, (void*)"AL16UTF16");
    }
#if defined(USE_ITHREADS)
    MUTEX_UNLOCK(&mng_lock);
#endif
}
/*}}}*/

/* cnx_detach{{{*/
void
cnx_detach(pTHX_ imp_dbh_t * imp_dbh)
{
    /* Oracle will commit on an orderly disconnect.	*/
    /* See DBI Driver.xst file for the DBI approach.	*/
#ifdef ORA_OCI_112
    if (imp_dbh->lock->refs < 0)
    {
        /* Release session, tagged for future retrieval. */
        if(imp_dbh->session_tag != NULL)
        {
            STRLEN tlen;
            char * tag = SvPV(imp_dbh->session_tag, tlen);
            (void)OCISessionRelease(
                    imp_dbh->svchp, imp_dbh->errhp,
                    (OraText*)tag, (ub4)tlen, OCI_SESSRLS_RETAG
            );
            SvREFCNT_dec(imp_dbh->session_tag);
            imp_dbh->session_tag = NULL;
        }
        else (void)OCISessionRelease(
                imp_dbh->svchp, imp_dbh->errhp,
                NULL, 0, OCI_DEFAULT
        );
        /* all handles are gone now */
        imp_dbh->seshp = NULL;
        imp_dbh->svchp = NULL;
        imp_dbh->srvhp = NULL;
    }
    else {
#endif
        (void)OCISessionEnd(
                imp_dbh->svchp, imp_dbh->errhp, imp_dbh->seshp, OCI_DEFAULT
        );
        (void)OCIServerDetach(imp_dbh->srvhp, imp_dbh->errhp, OCI_DEFAULT);
#ifdef ORA_OCI_112
    }
#endif
}/*}}}*/
/*cnx_clean{{{*/
void
cnx_clean(pTHX_ imp_dbh_t * imp_dbh)
{
    if(imp_dbh->errhp != NULL)
    {
        (void)OCIHandleFree(imp_dbh->errhp, OCI_HTYPE_ERROR);
        imp_dbh->errhp = NULL;
    }
    if(imp_dbh->seshp != NULL)
    {
        (void)OCIHandleFree(imp_dbh->seshp, OCI_HTYPE_SESSION);
        imp_dbh->seshp = NULL;
    }
    if(imp_dbh->svchp != NULL)
    {
        (void)OCIHandleFree(imp_dbh->svchp, OCI_HTYPE_SVCCTX);
        imp_dbh->svchp = NULL;
    }
    if(imp_dbh->srvhp != NULL)
    {
        (void)OCIHandleFree(imp_dbh->srvhp, OCI_HTYPE_SERVER);
        imp_dbh->srvhp = NULL;
    }

#ifdef ORA_OCI_112
    if(imp_dbh->lock->refs < 0)
    {
        imp_dbh->lock->refs++;
        if(imp_dbh->lock->refs == 0)
            release_pool(aTHX_ (pool_box_t *)imp_dbh->lock);
    }
    else
    {
#endif
        imp_dbh->lock->refs--;
        if(imp_dbh->lock->refs == 0)
            release_env(aTHX_ (env_box_t *)imp_dbh->lock);
#ifdef ORA_OCI_112
    }
#endif
}/*}}}*/

void
cnx_drop_dr(pTHX_ imp_drh_t * imp_drh)
{
    llist_t * el;
#if defined(USE_ITHREADS)
    MUTEX_LOCK(&mng_lock);
#endif
    dr_instances--;
    el = llist_left(&mng_list);
    while(el != &mng_list)
    {
        box_t * base = (box_t *)el;
#ifdef ORA_OCI_112
        int is_pool = 0;
        if(base->refs < 0)
        {
            base->refs++;
            is_pool = 1;
        }
        else
#endif
        base->refs--;
        el = llist_left(el);
        if(base->refs == 0)
        {
#ifdef ORA_OCI_112
            if(is_pool)
                release_pool(aTHX_ (pool_box_t *)base);
            else
#endif
                release_env(aTHX_ (env_box_t *)base);
        }
    }
#if defined(USE_ITHREADS)
    MUTEX_UNLOCK(&mng_lock);
#endif
}

/* in vim: set foldmethod=marker: */
