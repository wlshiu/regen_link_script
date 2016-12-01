/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file partial_read.h
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/01
 * @license
 * @description
 */

#ifndef __partial_read_H_wEvw3Yzn_l2k4_HBBC_sXGl_uAFLkimY3eSv__
#define __partial_read_H_wEvw3Yzn_l2k4_HBBC_sXGl_uAFLkimY3eSv__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
//=============================================================================
//                  Constant Definition
//=============================================================================

//=============================================================================
//                  Macro Definition
//=============================================================================

//=============================================================================
//                  Structure Definition
//=============================================================================
typedef struct partial_read
{
    FILE            *fp;

    unsigned char   *pBuf;
    int             buf_size;
    int             remain_length;

    unsigned char   *pEnd;

    int             alignment;
    unsigned long   is_big_endian;


} partial_read_t;
//=============================================================================
//                  Global Data Definition
//=============================================================================

//=============================================================================
//                  Private Function Definition
//=============================================================================

//=============================================================================
//                  Public Function Definition
//=============================================================================
static inline int
partial_read__full_buf(
    partial_read_t  *pHReader)
{
    int         rval = 0;
    return rval;
}


#ifdef __cplusplus
}
#endif

#endif
