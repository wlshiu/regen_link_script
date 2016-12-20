/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file cmp_symbol.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/04
 * @license
 * @description
 */



#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "partial_read.h"
#include "table_desc.h"
#include "crc32.h"
#include "regex.h"
//=============================================================================
//                  Constant Definition
//=============================================================================
#define MAX_BUFFER_SIZE                     (2 << 20)
//=============================================================================
//                  Macro Definition
//=============================================================================
#define err(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)

//=============================================================================
//                  Structure Definition
//=============================================================================
/**
 *  symbol table
 */
typedef struct symbol_table
{
    symbol_itm_t        *pSymbol_head;
    symbol_itm_t        *pSymbol_cur;

} symbol_table_t;

typedef struct region_info
{
    char    *pStart;
    char    *pEnd;

    unsigned long   addr_start;
    unsigned long   addr_end;

} region_info_t;
//=============================================================================
//                  Global Data Definition
//=============================================================================

//=============================================================================
//                  Private Function Definition
//=============================================================================
static int
_create_reader(
    partial_read_t  *pHReader,
    const char      *pPath)
{
    int     rval = 0;

    do {
        if( !pPath )
        {
            err("%s", "null path \n");
            rval = -1;
            break;
        }

        if( !(pHReader->fp = fopen(pPath, "rb")) )
        {
            err("open '%s' fail \n", pPath);
            rval = -1;
            break;
        }

        pHReader->buf_size = MAX_BUFFER_SIZE;
        if( !(pHReader->pBuf = malloc(pHReader->buf_size)) )
        {
            err("malloc '%ld' fail \n", pHReader->buf_size);
            rval = -1;
            break;
        }

        pHReader->pCur          = pHReader->pBuf;
        pHReader->pEnd          = pHReader->pCur;

        fseek(pHReader->fp, 0l, SEEK_END);
        pHReader->file_size = ftell(pHReader->fp);
        fseek(pHReader->fp, 0l, SEEK_SET);

        pHReader->file_remain = pHReader->file_size;

    } while(0);

    if( rval )
    {
        if( pHReader->fp )      fclose(pHReader->fp);
        pHReader->fp = 0;

        if( pHReader->pBuf )    free(pHReader->pBuf);
        pHReader->pBuf = 0;
    }

    return rval;
}

static int
_destroy_reader(
    partial_read_t  *pHReader)
{
    int         rval = 0;

    if( pHReader->fp )      fclose(pHReader->fp);
    if( pHReader->pBuf )    free(pHReader->pBuf);

    memset(pHReader, 0x0, sizeof(partial_read_t));
    return rval;
}

static int
_post_read(unsigned char *pBuf, int buf_size)
{
    int     i;
    for(i = 0; i < buf_size; ++i)
    {
        if( pBuf[i] == '\n' )
            pBuf[i] = '\0';
    }
    return 0;
}

static int
_create_symbol_table(
    partial_read_t      *pHReader,
    symbol_table_t      *pSymbol_table)
{
    int         rval = 0;

    partial_read__full_buf(pHReader, _post_read);
    while( pHReader->pCur < pHReader->pEnd )
    {
        if( partial_read__full_buf(pHReader, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            uint32_t        crc_id = 0;
            char            *pAct_str = 0;
            char            section_name[32] = {0};
            char            symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            unsigned long   is_lib_obj = 0;
            symbol_itm_t    *pSymbol_act = 0;

            pAct_str = (char*)pHReader->pCur;

            pHReader->pCur += (strlen((char*)pHReader->pCur) + 1);

            is_lib_obj = (strstr(pAct_str, ".o*")) ? 1 : 0;

            pAct_str = strchr(pAct_str, '*');
            if( !pAct_str )     continue;

            if( is_lib_obj )
            {
                // *lib_obj.o* (.text* )
                rval = sscanf(pAct_str, "*%[^*]* (.%[^*]* )", symbol_name, section_name);
                if( rval != 2 )
                {
                    continue;
                }
            }
            else
            {
                // * (.text.symbol_name)
                rval = sscanf(pAct_str, "* (.%[^.].%[^)])", section_name, symbol_name);
                if( rval != 2 )
                {
                    continue;
                }
            }

            if( strncmp(section_name, "text", 5) )     continue;

            crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

            if( pSymbol_table->pSymbol_head )
            {
                symbol_itm_t    *pSymbol_cur = pSymbol_table->pSymbol_head;

                while( pSymbol_cur )
                {
                    if( pSymbol_cur->crc_id == crc_id )
                    {
                        pSymbol_act = pSymbol_cur;
                        break;
                    }
                    pSymbol_cur = pSymbol_cur->next;
                }

                if( pSymbol_act )      continue;
            }

            if( !(pSymbol_act = malloc(sizeof(symbol_itm_t))) )
            {
                err("malloc '%d' fail \n", sizeof(symbol_itm_t));
                break;
            }
            memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

            pSymbol_act->crc_id = crc_id;
            snprintf(pSymbol_act->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);
            if( pSymbol_table->pSymbol_head )
            {
                pSymbol_table->pSymbol_cur->next = pSymbol_act;
                pSymbol_table->pSymbol_cur       = pSymbol_act;
            }
            else
            {
                pSymbol_table->pSymbol_head = pSymbol_table->pSymbol_cur = pSymbol_act;
            }
        }
    }

    return rval;
}

static int
_destroy_symbol_table(
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    do {
        symbol_itm_t    *pCur = 0;

        if( !pSymbol_table )    break;

        pCur = pSymbol_table->pSymbol_head;
        while( pCur )
        {
            symbol_itm_t    *pTmp = pCur;

            pCur = pCur->next;
            free(pTmp);
        }

        memset(pSymbol_table, 0x0, sizeof(symbol_table_t));
    } while(0);
    return rval;
}

static int
_dump_symbol_table(
    char            *pOut_name,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    do {
        symbol_itm_t    *pCur = 0;

        if( !pSymbol_table || !pOut_name )    break;

        if( !(fout = fopen(pOut_name, "wb")) )
        {
            err("open '%s' fail \n", pOut_name);
            break;
        }

        pCur = pSymbol_table->pSymbol_head;
        while( pCur )
        {
            fprintf(fout, "* (.text.%s)\n", pCur->symbol_name);
            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}
//=============================================================================
//                  Public Function Definition
//=============================================================================
int cmp_symbol(char *pList_1, char *pList_2)
{
    int                 rval = 0;
    FILE                *fout = 0;
    partial_read_t      hReader_list_a = {0};
    partial_read_t      hReader_list_b = {0};
    symbol_table_t      symbol_table_a = {0};
    symbol_table_t      symbol_table_b = {0};
    do {
        char                *pPath = 0;

        pPath = pList_1;
        hReader_list_a.alignment     = 0;
        hReader_list_a.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_list_a, pPath)) )
            break;

        _create_symbol_table(&hReader_list_a, &symbol_table_a);
        _dump_symbol_table("_a.txt", &symbol_table_a);

        pPath = pList_2;
        hReader_list_b.alignment     = 0;
        hReader_list_b.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_list_b, pPath)) )
            break;

        _create_symbol_table(&hReader_list_b, &symbol_table_b);
        _dump_symbol_table("_b.txt", &symbol_table_b);

        {   // start compare
            symbol_itm_t    *pSymbol_cur = 0;
            symbol_itm_t    *pSymbol_act = 0;

            if( !(fout = fopen("cmp_result.txt", "wb")))
            {
                err("open '%s' fail \n", "cmp_result.txt");
                break;
            }

            // base on symbol_table_b
            pSymbol_act = symbol_table_a.pSymbol_head;
            while( pSymbol_act )
            {
                unsigned long       is_match = 0;

                pSymbol_cur = symbol_table_b.pSymbol_head;
                while( pSymbol_cur )
                {
                    if( !strcmp(pSymbol_cur->symbol_name, "txtmode") )
                        printf("\n");
                    if( pSymbol_act->crc_id == pSymbol_cur->crc_id )
                    {
                        is_match = 1;
                        break;
                    }
                    pSymbol_cur = pSymbol_cur->next;
                }

                if( !is_match  )
                    fprintf(fout, "%s excess: '%s'\n", pList_1, pSymbol_act->symbol_name);

                pSymbol_act = pSymbol_act->next;
            }

            fprintf(fout, "\n\n");

            // base on symbol_table_a
            pSymbol_act = symbol_table_b.pSymbol_head;
            while( pSymbol_act )
            {
                unsigned long       is_match = 0;

                pSymbol_cur = symbol_table_a.pSymbol_head;
                while( pSymbol_cur )
                {
                    if( pSymbol_act->crc_id == pSymbol_cur->crc_id )
                    {
                        is_match = 1;
                        break;
                    }
                    pSymbol_cur = pSymbol_cur->next;
                }

                if( !is_match )
                    fprintf(fout, "%s excess: '%s'\n", pList_2, pSymbol_act->symbol_name);

                pSymbol_act = pSymbol_act->next;
            }
        }

    } while(0);

    if( fout )      fclose(fout);

    _destroy_reader(&hReader_list_a);
    _destroy_reader(&hReader_list_b);
    _destroy_symbol_table(&symbol_table_a);
    _destroy_symbol_table(&symbol_table_b);

    fprintf(stderr, "--------- done\n");
    return 0;
}


int calc_region_size(char *pPath, int argc, const char **ppStart_word, const char **ppEnd_word)
{
#define MAX_REGION_KEY_NUM          10
    int                 rval = 0;
    partial_read_t      hReader = {0};
    regex_t             hRegex = {0};

    do {
        int             i, pattern_cnt = 0;
        region_info_t   region_info[MAX_REGION_KEY_NUM] = {{0}};

        if( (rval = _create_reader(&hReader, pPath)) )
            break;

        pattern_cnt = (argc < MAX_REGION_KEY_NUM) ? argc : MAX_REGION_KEY_NUM;
        for(i = 0; i < pattern_cnt; ++i)
        {
            region_info[i].pStart = (char*)ppStart_word[i];
            region_info[i].pEnd   = (char*)ppEnd_word[i];
        }

        i = 0;
        partial_read__full_buf(&hReader, _post_read);
        while( hReader.pCur < hReader.pEnd )
        {
            if( partial_read__full_buf(&hReader, _post_read) )
            {
                break;
            }

            {   // start parsing a line
                char            *pAct_str = 0;

                pAct_str = (char*)hReader.pCur;

                hReader.pCur += (strlen((char*)hReader.pCur) + 1);
                for(i = 0; i < pattern_cnt; ++i)
                {
                    unsigned long       int_addr = 0l;
                    char                tmp_str[128] = {0};
                    size_t              nmatch = 2;
                    regmatch_t          match_info[2] = {{0}};

                    if( (strstr(pAct_str, region_info[i].pStart)) )
                    {
                        snprintf(tmp_str, 128, "^.*0x([0-9A-Fa-f]+)\\s+%s", region_info[i].pStart);
                        rval = regcomp(&hRegex, tmp_str, REG_EXTENDED);
                        if( rval )
                        {
                            char    msgbuf[256] = {0};
                            regerror(rval, &hRegex, msgbuf, sizeof(msgbuf));
                            err("%s\n", msgbuf);
                        }

                        rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
                        if( rval == REG_NOMATCH || rval )
                            continue;

                        memset(tmp_str, 0x0, 128);
                        if( match_info[1].rm_so != -1 )
                        {
                            strncpy(tmp_str, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                            int_addr = strtoul(tmp_str, NULL, 16);
                            region_info[i].addr_start = int_addr;
                        }
                    }

                    if( (strstr(pAct_str, region_info[i].pEnd)) )
                    {
                        snprintf(tmp_str, 128, "^.*0x([0-9A-Fa-f]+)\\s+%s", region_info[i].pEnd);
                        rval = regcomp(&hRegex, tmp_str, REG_EXTENDED);
                        if( rval )
                        {
                            char    msgbuf[256] = {0};
                            regerror(rval, &hRegex, msgbuf, sizeof(msgbuf));
                            err("%s\n", msgbuf);
                        }

                        rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
                        if( rval == REG_NOMATCH || rval )
                            continue;

                        memset(tmp_str, 0x0, 128);
                        if( match_info[1].rm_so != -1 )
                        {
                            strncpy(tmp_str, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                            int_addr = strtoul(tmp_str, NULL, 16);
                            region_info[i].addr_end = int_addr;
                        }
                    }
                }
            }
        }

        fprintf(stderr, "\n==================\n");
        for(i = 0; i < pattern_cnt; ++i)
        {
            region_info_t   *pCur_info = &region_info[i];

            fprintf(stderr, "region %02d: x%08lx -> x%08lx, (size= %ld)\n",
                    i, pCur_info->addr_start, pCur_info->addr_end,
                    pCur_info->addr_end - pCur_info->addr_start);
        }
        fprintf(stderr, "==================\n");
    } while(0);

    regfree(&hRegex);

    _destroy_reader(&hReader);

    return rval;
}

