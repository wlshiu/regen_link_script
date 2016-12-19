/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file table_symbols.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/18
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
        if( pBuf[i] == '\n' || pBuf[i] == '\r' )
            pBuf[i] = '\0';
    }
    return 0;
}

static int
symbols_create(table_op_args_t *pArgs)
{
    int                 rval = 0;
    regex_t             hRegex = {0};
    partial_read_t      *pHReader = 0;
    table_symbols_t     *pSymbol_table = 0;

    pHReader        = (partial_read_t*)pArgs->pTunnel_info;
    pSymbol_table   = pArgs->table.pTable_symbols;

    rval = regcomp(&hRegex, "(.*)\\s+(\\w)\\s+(.*)\\s+.*$", REG_EXTENDED);
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
            unsigned long       int_addr = 0l;
            char                *pAct_str = 0;
            char                symbol_type = 0;
            char                symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            size_t              nmatch = 4;
            regmatch_t          match_info[4] = {{0}};

            pAct_str = (char*)pHReader->pCur;

            pHReader->pCur += (strlen((char*)pHReader->pCur) + 1);

            rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
            if( rval == REG_NOMATCH || rval )
                continue;

            {
                char    *pTmp = symbol_name;

                memset(pTmp, 0x0, MAX_SYMBOL_NAME_LENGTH);
                if( match_info[1].rm_so != -1 )
                {
                    strncpy(pTmp, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                    int_addr = strtoul(pTmp, NULL, 16);
                }

                memset(pTmp, 0x0, MAX_SYMBOL_NAME_LENGTH);
                if( match_info[2].rm_so != -1 )
                {
                    if( match_info[2].rm_eo - match_info[2].rm_so == 1 )
                        symbol_type = pAct_str[match_info[2].rm_so];
                }

                memset(pTmp, 0x0, MAX_SYMBOL_NAME_LENGTH);
                if( match_info[3].rm_so != -1 )
                {
                    strncpy(symbol_name, &pAct_str[match_info[3].rm_so], match_info[3].rm_eo - match_info[3].rm_so);
                }
            }

            if( symbol_name[0] != '$' && symbol_name[0] != '.' &&
                (symbol_type == 'T' || symbol_type == 't') )
            {
                unsigned int    crc_id = 0;
                unsigned long   is_dummy = 0;
                symbol_itm_t    *pNew_item = 0;
                symbol_itm_t    *pCur_item = 0;

                crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

                // check the same symbol
                pCur_item = pSymbol_table->pSymbol_head;
                while( pCur_item )
                {
                    if( pCur_item->crc_id == crc_id )
                    {
                        // err_msg("get the same symbol name '%s'\n", pCur_item->symbol_name);
                        is_dummy = 1;
                        break;
                    }
                    pCur_item = pCur_item->next;
                }

                if( is_dummy )      continue;

                if( !(pNew_item = malloc(sizeof(symbol_itm_t))) )
                {
                    err_msg("malloc symbol item (%d) fail \n", sizeof(symbol_itm_t));
                    break;
                }
                memset(pNew_item, 0x0, sizeof(symbol_itm_t));

                pNew_item->pAddr  = (void*)int_addr;
                pNew_item->crc_id = crc_id;
                snprintf(pNew_item->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);

                // add to list
                if( pSymbol_table->pSymbol_head )
                {
                    pSymbol_table->pSymbol_cur->next = pNew_item;
                    pSymbol_table->pSymbol_cur       = pNew_item;
                }
                else
                {
                    pSymbol_table->pSymbol_head = pSymbol_table->pSymbol_cur = pNew_item;
                }
            }
        }
    }

    regfree(&hRegex);

    return 0; //rval;
}

static int
symbols_destroy(table_op_args_t *pArgs)
{
    int             rval = 0;

    do {
        table_symbols_t     *pSymbol_table = pArgs->table.pTable_symbols;
        symbol_itm_t        *pCur = 0;

        if( !pSymbol_table )    break;

        pCur = pSymbol_table->pSymbol_head;
        while( pCur )
        {
            symbol_itm_t    *pTmp = pCur;

            pCur = pCur->next;
            free(pTmp);
        }

        memset(pSymbol_table, 0x0, sizeof(table_symbols_t));
    } while(0);

    return rval;
}

static int
symbols_dump(table_op_args_t *pArgs)
{
    int         rval = 0;
    FILE        *fout = 0;

    do {
        char                *pOut_name = pArgs->pOut_name;
        table_symbols_t     *pSymbol_table = pArgs->table.pTable_symbols;
        symbol_itm_t        *pCur = 0;

        if( !pSymbol_table || !pOut_name )    break;

        if( !(fout = fopen(pOut_name, "wb")) )
        {
            err_msg("open '%s' fail \n", pOut_name);
            break;
        }

        pCur = pSymbol_table->pSymbol_head;
        while( pCur )
        {
            char    lib_crc_str[64] = {0};
            snprintf(lib_crc_str, 64, "# lib_crc= x%08x", pCur->lib_crc_id);
            fprintf(fout, "* (.text.%s*)\t\t%s\n", pCur->symbol_name, (pCur->lib_crc_id) ? lib_crc_str : "");
            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}
//=============================================================================
//                  Public Function Definition
//=============================================================================
table_ops_t     table_symbols_desc =
{
    .next       = NULL,
    .tab_id     = TABLE_ID_SYMBOLS,
    .pf_create  = symbols_create,
    .pf_destroy = symbols_destroy,
    .pf_dump    = symbols_dump,
};
