/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file symbol_info.h
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/02
 * @license
 * @description
 */

#ifndef __symbol_info_H_woCv13oI_lcMc_HcSP_spTU_u6TgSCqn1rK8__
#define __symbol_info_H_woCv13oI_lcMc_HcSP_spTU_u6TgSCqn1rK8__

#ifdef __cplusplus
extern "C" {
#endif


//=============================================================================
//                  Constant Definition
//=============================================================================
#define MAX_SYMBOL_NAME_LENGTH       256
//=============================================================================
//                  Macro Definition
//=============================================================================

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
