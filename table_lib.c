/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file table_lib.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/19
 * @license
 * @description
 */


#include "table_desc.h"
#include "crc32.h"
#include "partial_read.h"
#include "regex.h"

//=============================================================================
//                  Constant Definition
//=============================================================================

//=============================================================================
//                  Macro Definition
//=============================================================================

//=============================================================================
//                  Structure Definition
//=============================================================================

//=============================================================================
//                  Global Data Definition
//=============================================================================

//=============================================================================
//                  Private Function Definition
//=============================================================================
static int
_post_read(unsigned char *pBuf, int buf_size)
{
    int     i;
    for(i = 0; i < buf_size; ++i)
    {
        unsigned char   value = pBuf[i];

        if( value == '\n' || value == '\r' )
            pBuf[i] = '\0';

        if( value == '\\' )
            pBuf[i] = '/';
    }
    return 0;
}

static int
lib_obj_create(table_op_args_t *pArgs)
{
    int                 rval = 0;
    regex_t             hRegex = {0};
    partial_read_t      *pHReader = 0;
    table_lib_t         *pLib_table = 0;

    pHReader    = (partial_read_t*)pArgs->pTunnel_info;
    pLib_table  = pArgs->table.pTable_lib;

    rval = regcomp(&hRegex, ".*/(.*)\\.(.*)\\((.*)\\.(.*)\\)", REG_EXTENDED);
    if( rval )
    {
        char    msgbuf[256] = {0};
        regerror(rval, &hRegex, msgbuf, sizeof(msgbuf));
        printf("%s\n", msgbuf);
    }

    // generate symbol database
    partial_read__full_buf(pHReader, _post_read);
    while( pHReader->pCur < pHReader->pEnd )
    {
        if( partial_read__full_buf(pHReader, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            uint32_t            crc_id = 0;
            char                *pAct_str = 0;
            char                lib_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            char                obj_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            lib_itm_t           *pAct_lib = 0;
            obj_itm_t           *pAct_obj = 0;
            size_t              nmatch = 5;
            regmatch_t          match_info[5] = {{0}};

            pAct_str = (char*)pHReader->pCur;

            pHReader->pCur += (strlen((char*)pHReader->pCur) + 1);

            rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
            if( rval == REG_NOMATCH || rval )
                continue;

            {
                if( match_info[1].rm_so != -1 )
                {
                    strncpy(lib_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                }

                if( match_info[2].rm_so != -1 )
                {
                    if( match_info[2].rm_eo - match_info[2].rm_so != 1 ||
                        pAct_str[match_info[2].rm_so] != 'a')
                    {
                        // err_msg("wrong lib name '%s.%c'\n", lib_name, pAct_str[match_info[2].rm_so]);
                        continue;
                    }
                }

                if( match_info[3].rm_so != -1 )
                {
                    strncpy(obj_name, &pAct_str[match_info[3].rm_so], match_info[3].rm_eo - match_info[3].rm_so);
                }

                if( match_info[4].rm_so != -1 )
                {
                    if( match_info[4].rm_eo - match_info[4].rm_so != 1 ||
                        pAct_str[match_info[4].rm_so] != 'o')
                    {
                        // err_msg("wrong obj name '%s.%c'\n", obj_name, pAct_str[match_info[4].rm_so]);
                        continue;
                    }
                }
            }

            //---------------------------------------
            // check lib exist or not in table
            crc_id = calc_crc32((uint8_t*)lib_name, strlen(lib_name));
            if( pLib_table->pLib_head )
            {
                lib_itm_t           *pCur_lib = pLib_table->pLib_head;
                while( pCur_lib )
                {
                    if( pCur_lib->crc_id == crc_id )
                    {
                        pAct_lib = pCur_lib;
                        break;
                    }
                    pCur_lib = pCur_lib->next;
                }
            }

            if( !pAct_lib )
            {
                if( !(pAct_lib = malloc(sizeof(lib_itm_t))) )
                {
                    err_msg("malloc '%d' fail \n", sizeof(lib_itm_t));
                    break;
                }
                memset(pAct_lib, 0x0, sizeof(lib_itm_t));

                pAct_lib->crc_id = crc_id;
                snprintf(pAct_lib->lib_name, MAX_SYMBOL_NAME_LENGTH, "%s.a", lib_name);

                if( pLib_table->pLib_head )
                {
                    pLib_table->pLib_cur->next = pAct_lib;
                    pLib_table->pLib_cur       = pAct_lib;
                }
                else
                {
                    pLib_table->pLib_head = pLib_table->pLib_cur = pAct_lib;
                }
            }

            //---------------------------------------
            // check obj file exist or not in the lib_itm
            crc_id = calc_crc32((uint8_t*)obj_name, strlen(obj_name));
            if( pAct_lib->pObj_head )
            {
                obj_itm_t           *pCur_obj = pAct_lib->pObj_head;
                while( pCur_obj )
                {
                    if( pCur_obj->crc_id == crc_id )
                    {
                        pAct_obj = pCur_obj;
                        break;
                    }

                    pCur_obj = pCur_obj->next;
                }
            }

            if( pAct_obj )      continue;

            if( !(pAct_obj = malloc(sizeof(obj_itm_t))) )
            {
                err_msg("malloc '%d' fail \n", sizeof(obj_itm_t));
                break;
            }
            memset(pAct_obj, 0x0, sizeof(obj_itm_t));

            pAct_obj->crc_id   = crc_id;
            snprintf(pAct_obj->obj_name, MAX_SYMBOL_NAME_LENGTH, "%s.o", obj_name);
            if(  pAct_lib->pObj_head )
            {
                pAct_lib->pObj_cur->next = pAct_obj;
                pAct_lib->pObj_cur       = pAct_obj;
            }
            else
            {
                pAct_lib->pObj_head =  pAct_lib->pObj_cur = pAct_obj;
            }
        }
    }

    regfree(&hRegex);

    return rval;
}

static int
lib_obj_destroy(table_op_args_t *pArgs)
{
    int             rval = 0;

    do {
        table_lib_t     *pLib_table = pArgs->table.pTable_lib;
        lib_itm_t       *pCur = 0;
        obj_itm_t       *pCur_obj = 0;

        if( !pLib_table)     break;

        pCur = pLib_table->pLib_head;
        while( pCur )
        {
            lib_itm_t      *pTmp = pCur;

            pCur_obj = pCur->pObj_head;
            while( pCur_obj )
            {
                obj_itm_t    *pTmp_ojb = pCur_obj;

                pCur_obj = pCur_obj->next;
                free(pTmp_ojb);
            }

            pCur = pCur->next;
            free(pTmp);
        }

        memset(pLib_table, 0x0, sizeof(table_lib_t));
    } while(0);

    return rval;
}

static int
lib_obj_dump(table_op_args_t *pArgs)
{
    int         rval = 0;
    FILE        *fout = 0;

    do {
        char            *pOut_name = pArgs->pOut_name;
        table_lib_t     *pLib_table = pArgs->table.pTable_lib;
        lib_itm_t       *pCur = 0;
        obj_itm_t       *pCur_obj = 0;

        if( !pLib_table)     break;

        if( !(fout = fopen(pOut_name, "wb")) )
        {
            err_msg("open '%s' fail \n", pOut_name);
            break;
        }

        pCur = pLib_table->pLib_head;
        while( pCur )
        {
            fprintf(fout, "\n/* %s, crc_id= x%08x */\n", pCur->lib_name, pCur->crc_id);
            pCur_obj = pCur->pObj_head;
            while( pCur_obj )
            {
                fprintf(fout, "    *%s* (.text* )\n", pCur_obj->obj_name);
                pCur_obj = pCur_obj->next;
            }

            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}
//=============================================================================
//                  Public Function Definition
//=============================================================================
table_ops_t     table_lib_obj_desc =
{
    .next       = NULL,
    .tab_id     = TABLE_ID_LIB_OBJ,
    .pf_create  = lib_obj_create,
    .pf_destroy = lib_obj_destroy,
    .pf_dump    = lib_obj_dump,
};
