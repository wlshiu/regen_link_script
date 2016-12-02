/**
 * Copyright (c) 2015 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file mleak_check.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2015/10/30
 * @license
 * @description
 */


#include "mleak_check.h"
#undef malloc
#undef free

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
//=============================================================================
//                Constant Definition
//=============================================================================
#define MLEAK_PROTECT_PATTERN       0x55
/**
 *  1 unit is 4 bytes
 */
#define MLEAK_PROTECT_SIZE          1

/**
 *  FOUR_CC('m', 'e', 'm', 'k')
 */
#define MLEAK_VERIFY_TAG            0X6D656D6B
//=============================================================================
//                Macro Definition
//=============================================================================
#define FOUR_CC(a, b, c, d)         (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#if 1
    #define _err(st, args...)       do{fprintf(stderr, "[%u] %s: " st, __LINE__, __FUNCTION__, ## args); }while(0)
    #define _dbg(st, args...)       fprintf(stderr, st, ## args)
#else
    #define _err(st, args...)
    #define _dbg(st, args...)
#endif
//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  info of malloc
 */
typedef struct mleak_mem_node
{
    unsigned int    verify_tag;

    struct mleak_mem_node   *prev, *next;

    // physical memory
    void            *pBase;
//    void            *pAlignment;

    // malloc info
//    const char      *pName;
    void            *pPtr;
    unsigned int    length;
    unsigned int    line_num;
    char            path[256];

} mleak_mem_node_t;

/**
 *
 */
typedef struct mleak_dev
{
    mleak_mem_node_t        *pNode_head;
    mleak_mem_node_t        *pNode_cur;

    int                     node_cnt;
} mleak_dev_t;
//=============================================================================
//                Global Data Definition
//=============================================================================
static mleak_dev_t  g_mleak_dev = {0};

static pthread_mutex_t  g_mleak_mutex = PTHREAD_MUTEX_INITIALIZER;
//=============================================================================
//                Private Function Definition
//=============================================================================
static int
_node_add(
    mleak_mem_node_t    *pNode)
{
    if( !g_mleak_dev.pNode_head )
    {
        g_mleak_dev.pNode_head = g_mleak_dev.pNode_cur = pNode;
        g_mleak_dev.node_cnt = 1;
    }
    else
    {
        mleak_mem_node_t    *pCur = g_mleak_dev.pNode_cur;

        pNode->next = 0;
        pCur->next  = pNode;
        pNode->prev = pCur;

        g_mleak_dev.pNode_cur = pNode;
        g_mleak_dev.node_cnt++;
    }

    return 0;
}

static mleak_mem_node_t*
_node_find_head(
    mleak_mem_node_t    *pCurNode)
{
    mleak_mem_node_t    *pNode = pCurNode;
    while( pNode && pNode->prev )
        pNode = pNode->prev;

    return pNode;
}

static mleak_mem_node_t*
_node_find_tail(
    mleak_mem_node_t    *pCurNode)
{
    mleak_mem_node_t    *pNode = pCurNode;
    while( pNode && pNode->next )
        pNode = pNode->next;

    return pNode;
}

static mleak_mem_node_t*
_node_del(
    mleak_mem_node_t    *pNode)
{
    mleak_mem_node_t    *pHead = 0;
    if( pNode )
    {
        if( pNode->prev )
            pNode->prev->next = pNode->next;
        if( pNode->next )
            pNode->next->prev = pNode->prev;

        if( pNode->prev )
            pHead = _node_find_head(pNode->prev);
        else if( pNode->next )
            pHead = _node_find_head(pNode->next);

        pNode->prev = pNode->next = 0;
        g_mleak_dev.node_cnt--;
    }

    return pHead;
}

static mleak_mem_node_t*
_node_find(
    unsigned long    ptr)
{
    mleak_mem_node_t    *pCur = g_mleak_dev.pNode_head;
    mleak_mem_node_t    *pTarget = 0;

    while( pCur )
    {
        if( pCur->verify_tag != MLEAK_VERIFY_TAG )
        {
            _err("fail, node is dirty !!\n");
            break;
        }

        if( (unsigned long)((void*)(pCur->pPtr)) == ptr )
        {
            pTarget = pCur;
            break;
        }

        pCur = pCur->next;
    }

    return pTarget;
}
//=============================================================================
//                Public Function Definition
//=============================================================================
void*
mleak_malloc(
    unsigned int        length,
    const char          *pPath,
    const unsigned int  line_num)
{
    unsigned int        total_size;
    mleak_mem_node_t    *pNode = 0;
    unsigned char       *pMem = 0;

    /*    node header   4 bytes protect     request mem     PROTECT_SIZE (n * 4 bytes)
     *  |-------------|-----------------|-----------------|----------------------------|
     */
    total_size = sizeof(mleak_mem_node_t) + 4 + length + (MLEAK_PROTECT_SIZE << 2) + 4;
    if( !(pMem = malloc(total_size)) )
    {
        _err("allocate fail !\n");
        return NULL;
    }
    memset(pMem, MLEAK_PROTECT_PATTERN, total_size);

    pNode = (mleak_mem_node_t*)(((unsigned long)(void*)pMem + 3) & ~0x3);

    pNode->verify_tag = MLEAK_VERIFY_TAG;
    pNode->prev = pNode->next = 0;

    pNode->pBase    = pMem;
    pNode->pPtr     = (unsigned char*)pNode + sizeof(mleak_mem_node_t) + 4;
    pNode->length   = length;
    pNode->line_num = line_num;
    snprintf(pNode->path, 256, "%s", pPath);

    memset(pNode->pPtr, 0x0, pNode->length);

    pthread_mutex_lock(&g_mleak_mutex);
    _node_add(pNode);
    pthread_mutex_unlock(&g_mleak_mutex);
    return pNode->pPtr;

}

void
mleak_free(
    void                *ptr,
    const char          *name,
    const char          *path,
    const unsigned int  line_num)
{
    mleak_mem_node_t    *pNode_act = 0;

    pthread_mutex_lock(&g_mleak_mutex);

    pNode_act = _node_find((unsigned long)ptr);
    if( !pNode_act )
    {
        pthread_mutex_unlock(&g_mleak_mutex);
        _err("no x%p pointer to free, caller:%s #%d !!\n", ptr, path, line_num);
        mlead_dump();
        return;
    }

    g_mleak_dev.pNode_head = _node_del(pNode_act);
    g_mleak_dev.pNode_cur  = _node_find_tail(g_mleak_dev.pNode_head);

    pthread_mutex_unlock(&g_mleak_mutex);

#if 1
    {// check protect pattern
        int             i;
        int             bDirty = 0;
        unsigned char   *pTmp = 0;

        if( pNode_act->verify_tag != MLEAK_VERIFY_TAG )
        {
            _err("error, free a dirty pointer !!\n"
                 "\t%s at %s [#%u]\n", name, path, line_num);
            return;
        }

        pTmp = (unsigned char*)((unsigned long)ptr - 4);
        for(i = 0; i < 4; i++)
        {
            if( *(pTmp + i) != MLEAK_PROTECT_PATTERN )
            {
                bDirty = 1;
                break;
            }
        }

        pTmp = (unsigned char*)((unsigned long)ptr + pNode_act->length);
        for(i = 0; i < (MLEAK_PROTECT_SIZE << 2); i++)
        {
            if( *(pTmp + i) != MLEAK_PROTECT_PATTERN )
            {
                bDirty = 1;
                break;
            }
        }
        if( bDirty )
        {
            _dbg("memory is polluted !!\n"
                 "\tfree %s at %s [#%u]\n"
                 "\tmalloc at %s [#%u]\n",
                 name, path, line_num,
                 pNode_act->path, pNode_act->line_num);

            pTmp = (unsigned char*)((unsigned long)ptr - 4);
            _dbg("\t  hex: %02x %02x %02x %02x\n",
                 pTmp[0], pTmp[1], pTmp[2], pTmp[3]);

            pTmp = (unsigned char*)((unsigned long)ptr + pNode_act->length);
            _dbg("\t\t");
            for(i = 0; i < (MLEAK_PROTECT_SIZE << 2); i++)
                _dbg("%02x ", *(pTmp + i));

            _dbg("\n");
        }
    }
#endif

    free(pNode_act->pBase);
    return;
}

void
mlead_dump(void)
{
    mleak_mem_node_t    *pNode_act = 0;

    pthread_mutex_lock(&g_mleak_mutex);
    {
        pNode_act = g_mleak_dev.pNode_head;

        if( pNode_act )     _dbg("\n\nmemory leak:\n");

        while( pNode_act )
        {
            int             i;
            unsigned char   *pTmp = 0;

            if( pNode_act->verify_tag != MLEAK_VERIFY_TAG )
            {
                _err("error, get a dirty node !!\n");
                break;
            }

            _dbg("\tmalloc (ptr=x%p, len=%u) at %s [#%u]\n",
                 pNode_act->pPtr, pNode_act->length, pNode_act->path, pNode_act->line_num);

            pTmp = (unsigned char*)((unsigned long)pNode_act + sizeof(mleak_mem_node_t));
            _dbg("\t  hex: ");
            for(i = 0; i < 12; i++)
            {
                if( i == 4 )    _dbg("\n\t\t");

                _dbg("%02x ", *(pTmp + i));
            }

            _dbg("...\n\t\t");
            pTmp = (unsigned char*)((unsigned long)pNode_act->pPtr + pNode_act->length);
            for(i = 0; i < (MLEAK_PROTECT_SIZE << 2); i++)
                _dbg("%02x ", *(pTmp + i));

            _dbg("\n");
            pNode_act = pNode_act->next;
        }
        _dbg("-------------- %s end\n", __FUNCTION__);
    }
    pthread_mutex_unlock(&g_mleak_mutex);
    return;
}


