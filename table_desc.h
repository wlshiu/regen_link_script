/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file table_desc.h
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/15
 * @license
 * @description
 */

#ifndef __table_desc_H_w0HvN037_lUWv_HSXT_scFq_um9YCa44j00B__
#define __table_desc_H_w0HvN037_lUWv_HSXT_scFq_um9YCa44j00B__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
//=============================================================================
//                  Constant Definition
//=============================================================================
#define MAX_SYMBOL_NAME_LENGTH       128

typedef enum table_id
{
    TABLE_ID_SYMBOLS     = 0xb01,
    TABLE_ID_CALL_GRAPH,
    TABLE_ID_LIB_OBJ,
    TABLE_ID_SYMBOL_WITH_LIB_OBJ,

    TABLE_ID_TEST,
} table_id_t;
//=============================================================================
//                  Macro Definition
//=============================================================================
#ifndef err
    #define err_msg(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)
#endif

#define register_table(name, pHead)                 \
    do{ extern table_ops_t   table_##name##_desc;   \
        table_ops_t   **p;                          \
        p = &pHead;                                 \
        while (*p) p = &(*p)->next;                 \
        *p = &table_##name##_desc;                  \
        table_##name##_desc.next = 0;               \
    }while(0)
//=============================================================================
//                  Structure Definition
//=============================================================================
/**
 *  a symbol item
 */
struct symbol_itm;
typedef struct symbol_itm
{
    struct symbol_itm   *next;

    void            *pAddr;
    char            symbol_name[MAX_SYMBOL_NAME_LENGTH];
    unsigned int    crc_id;
    unsigned int    lib_crc_id;
    unsigned int    obj_crc_id;

    unsigned int    crc_mark_id;

    unsigned int    is_outputted;
    unsigned int    is_leaf;

} symbol_itm_t;


/**
 *  call graph relation
 */
struct symbol_relation;
typedef struct symbol_relation
{
    struct symbol_relation      *next;

    char            symbol_name[MAX_SYMBOL_NAME_LENGTH];
    unsigned int    crc_id;

    symbol_itm_t        *pCallee_list_head;
    symbol_itm_t        *pCallee_list_cur;

} symbol_relation_t;


/**
 *  object file item
 */
struct obj_itm;
typedef struct obj_itm
{
    struct obj_itm      *next;

    char            obj_name[MAX_SYMBOL_NAME_LENGTH];
    unsigned int    crc_id;
} obj_itm_t;


/**
 *  static lib item
 */
struct lib_itm;
typedef struct lib_itm
{
    struct lib_itm      *next;

    char            lib_name[MAX_SYMBOL_NAME_LENGTH];
    unsigned int    crc_id;

    obj_itm_t       *pObj_head;
    obj_itm_t       *pObj_cur;

    unsigned int    is_outputted;

} lib_itm_t;

/**
 *  symbol table
 */
typedef struct table_symbols
{
    symbol_itm_t        *pSymbol_head;
    symbol_itm_t        *pSymbol_cur;

} table_symbols_t;


/**
 *  call graph table
 */
typedef struct table_call_graph
{
    symbol_relation_t        *pSymbol_head;
    symbol_relation_t        *pSymbol_cur;

} table_call_graph_t;

/**
 *  static lib table
 */
typedef struct table_lib
{
    lib_itm_t       *pLib_head;
    lib_itm_t       *pLib_cur;

} table_lib_t;


/**
 *  arguments
 */
typedef struct table_op_args
{
    char                *pOut_name;

    void                *pTunnel_info;

    union {
        table_symbols_t     *pTable_symbols;
        table_call_graph_t  *pTable_call_graph;
        table_lib_t         *pTable_lib;
    } table;

} table_op_args_t;

/**
 *  description
 */
struct table_ops_t;
typedef struct table_ops_t
{
    struct table_ops_t      *next;
    table_id_t     tab_id;

    int     (*pf_create)(table_op_args_t *pArgs);
    int     (*pf_destroy)(table_op_args_t *pArgs);
    int     (*pf_dump)(table_op_args_t *pArgs);

} table_ops_t;



//=============================================================================
//                  Global Data Definition
//=============================================================================

//=============================================================================
//                  Private Function Definition
//=============================================================================

//=============================================================================
//                  Public Function Definition
//=============================================================================

#ifdef __cplusplus
}
#endif

#endif
