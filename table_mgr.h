/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file table_mgr.h
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/18
 * @license
 * @description
 */

#ifndef __table_mgr_H_wdjQ3Yls_lD9A_Hq80_sYeK_ubogRoAUhftx__
#define __table_mgr_H_wdjQ3Yls_lD9A_Hq80_sYeK_ubogRoAUhftx__

#ifdef __cplusplus
extern "C" {
#endif

#include "table_desc.h"
//=============================================================================
//                  Constant Definition
//=============================================================================

//=============================================================================
//                  Macro Definition
//=============================================================================

//=============================================================================
//                  Structure Definition
//=============================================================================
typedef void*   table_mgr_t;

/**
 *  all info for output
 */
typedef struct out_info
{
    table_symbols_t     *pSymbol_table_finial;
    table_symbols_t     *pSymbol_table_leaf;
    table_lib_t         *pLib_table;

} out_info_t;
//=============================================================================
//                  Global Data Definition
//=============================================================================

//=============================================================================
//                  Private Function Definition
//=============================================================================

//=============================================================================
//                  Public Function Definition
//=============================================================================
int
table_mgr__init(
    table_mgr_t     **ppMgr);


int
table_mgr__deinit(
    table_mgr_t     **ppMgr);


int
table_mgr__create_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs);


int
table_mgr__destroy_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs);


int
table_mgr__dump_table(
    table_mgr_t     *pMgr,
    table_id_t      tab_id,
    table_op_args_t *pArgs);



#ifdef __cplusplus
}
#endif

#endif
