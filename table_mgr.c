/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file table_mgr.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/18
 * @license
 * @description
 */

#include <stdlib.h>
#include <string.h>
#include "table_desc.h"
#include "table_mgr.h"
#include "pthread.h"
#include "regex.h"
//=============================================================================
//                  Constant Definition
//=============================================================================

//=============================================================================
//                  Macro Definition
//=============================================================================
#if 1
    #define STRUCTURE_POINTER(type, ptr, member)            (type*)ptr
#else
#ifndef MEMBER_OFFSET
    #define MEMBER_OFFSET(type, member)     (unsigned long)&(((type *)0)->member)
#endif

#ifndef STRUCTURE_POINTER
    #define STRUCTURE_POINTER(type, ptr, member)    (type*)((unsigned long)ptr - MEMBER_OFFSET(type, member))
#endif
#endif

#define ERR_CODE            (-__LINE__)
#define _str(x)             #x

#define _verify_handle(phandle, err_code)              \
            do{ if(phandle==NULL) {                    \
                    return err_code;}                  \
            }while(0)


#define _mutex_init(pMutex)       pthread_mutex_init((pMutex), NULL)
#define _mutex_deinit(pMutex)     pthread_mutex_destroy((pMutex))
#define _mutex_lock(pMutex)       pthread_mutex_lock((pMutex))
#define _mutex_unlock(pMutex)     pthread_mutex_unlock((pMutex))

#ifndef register_tab1e
#define register_tab1e(name, pHead)                 \
    do{ table_ops_t   **p;                          \
        p = &pHead;                                 \
        while (*p) p = &(*p)->next;                 \
        *p = (table_ops_t*)reggetinfo(_str(name));  \
        (*p)->next = 0;                             \
    }while(0)
#endif

#define find_table(tid, ppDesc)                     \
    do{ table_ops_t *desc = g_table_head;           \
        while( desc ) {                             \
            if( desc->tab_id == tid ) {             \
                *ppDesc = desc; break;              \
            }                                       \
            desc = desc->next;                      \
        }                                           \
    } while(0)
//=============================================================================
//                  Structure Definition
//=============================================================================
typedef struct table_mgr_dev
{
    pthread_mutex_t     mtx;

    table_ops_t         *pCur_table;

} table_mgr_dev_t;
//=============================================================================
//                  Global Data Definition
//=============================================================================
static table_ops_t      *g_table_head = 0;
//=============================================================================
//                  Private Function Definition
//=============================================================================
static void
_register_table(void)
{
    static int  is_initialized = 0;

    if( is_initialized )    return;

    register_table(symbols, g_table_head);
    register_tab1e(call_graph, g_table_head);
    register_table(lib_obj, g_table_head);
    register_tab1e(symbol_with_lib_obj, g_table_head);

    is_initialized = 1;
    return;
}
//=============================================================================
//                  Public Function Definition
//=============================================================================
int
table_mgr__init(
    table_mgr_t     **ppMgr)
{
    int                 rval = 0;
    table_mgr_dev_t     *pDev = 0;

    do {
        if( !ppMgr || *ppMgr )
        {
            err_msg("wrong parameters %p, %p\n", ppMgr, *ppMgr);
            rval = ERR_CODE;
            break;
        }

        if( !(pDev = malloc(sizeof(table_mgr_dev_t))) )
        {
            err_msg("malloc %d fail \n", sizeof(table_mgr_dev_t));
            rval = ERR_CODE;
            break;
        }
        memset(pDev, 0x0, sizeof(table_mgr_dev_t));

        _mutex_init(pDev->mtx);

        _register_table();

        *ppMgr = (table_mgr_t*)pDev;
    } while(0);

    return rval;
}

int
table_mgr__deinit(
    table_mgr_t     **ppMgr)
{
    int     rval = 0;

    if( ppMgr || *ppMgr )
    {
        table_mgr_dev_t     *pDev = STRUCTURE_POINTER(table_mgr_dev_t, (*ppMgr), mgr);
        pthread_mutex_t     mtx;

        _mutex_lock(&pDev->mtx);
        mtx = pDev->mtx;
        free(pDev);

        _mutex_unlock(&mtx);
        _mutex_deinit(&mtx);
    }
    return rval;
}

int
table_mgr__create_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs)
{
    int                 rval = 0;
    table_mgr_dev_t     *pDev = STRUCTURE_POINTER(table_mgr_dev_t, pMgr, mgr);

    _verify_handle(pMgr, ERR_CODE);

    _mutex_lock(&pDev->mtx);
    do {
        table_ops_t         *pTable_desc = 0;

        find_table(tab_id, &pTable_desc);
        if( !pTable_desc )
        {
            rval = ERR_CODE;
            break;
        }

        pDev->pCur_table = pTable_desc;
        if( pTable_desc->pf_create )
        {
            rval = pTable_desc->pf_create(pArgs);
            if( rval )
                break;
        }
    } while(0);

    _mutex_unlock(&pDev->mtx);
    return rval;
}

int
table_mgr__destroy_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs)
{
    int     rval = 0;
    table_mgr_dev_t     *pDev = STRUCTURE_POINTER(table_mgr_dev_t, pMgr, mgr);

    _verify_handle(pMgr, ERR_CODE);

    _mutex_lock(&pDev->mtx);
    do {
        table_ops_t         *pTable_desc = 0;

        find_table(tab_id, &pTable_desc);
        if( !pTable_desc )
        {
            rval = ERR_CODE;
            break;
        }

        if( pTable_desc && pTable_desc->pf_destroy )
        {
            rval = pTable_desc->pf_destroy(pArgs);
            if( rval )
                break;
        }
    } while(0);

    _mutex_unlock(&pDev->mtx);
    return rval;
}

int
table_mgr__dump_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs)
{
    int     rval = 0;
    table_mgr_dev_t     *pDev = STRUCTURE_POINTER(table_mgr_dev_t, pMgr, mgr);

    _verify_handle(pMgr, ERR_CODE);

    _mutex_lock(&pDev->mtx);
    do {
        table_ops_t         *pTable_desc = 0;

        find_table(tab_id, &pTable_desc);
        if( !pTable_desc )
        {
            rval = ERR_CODE;
            break;
        }

        if( pTable_desc && pTable_desc->pf_dump )
        {
            rval = pTable_desc->pf_dump(pArgs);
            if( rval )
                break;
        }
    } while(0);

    _mutex_unlock(&pDev->mtx);
    return rval;
}

