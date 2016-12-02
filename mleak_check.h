/**
 * Copyright (c) 2015 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file mleak_check.h
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2015/10/30
 * @license
 * @description
 */

#ifndef __mleak_check_H_wzrr32Il_l3Kl_H9zE_sRY9_uWh2F4EUey2c__
#define __mleak_check_H_wzrr32Il_l3Kl_H9zE_sRY9_uWh2F4EUey2c__

#ifdef __cplusplus
extern "C" {
#endif


//=============================================================================
//                Constant Definition
//=============================================================================

//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================

//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================

//=============================================================================
//                Public Function Definition
//=============================================================================
void*
mleak_malloc(
    unsigned int        length,
    const char          *pPath,
    const unsigned int  line_num);


void
mleak_free(
    void                *ptr,
    const char          *name,
    const char          *pPath,
    const unsigned int  line_num);


void
mlead_dump(void);


#if 1 //def _DEBUG

#undef malloc
//#define malloc(x)   mleak_malloc((x), __FILE__, __LINE__)
#define malloc(x)   mleak_malloc((x), __func__, __LINE__)

#undef free
//#define free(x)     mleak_free(x, #x, __FILE__, __LINE__)
#define free(x)     mleak_free(x, #x, __func__, __LINE__)

#endif


#ifdef __cplusplus
}
#endif

#endif