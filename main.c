/**
 * Copyright (c) 2016 Wei-Lun Hsu. All Rights Reserved.
 */
/** @file msin.c
 *
 * @author Wei-Lun Hsu
 * @version 0.1
 * @date 2016/12/18
 * @license
 * @description
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "table_desc.h"
#include "table_mgr.h"
#include "iniparser.h"
#include "crc32.h"
#include "partial_read.h"
#include "regex.h"
//=========================================================
#define MAX_BUFFER_SIZE                     (2 << 20)

#define FILE_NAME__BASIC_FUNC_FLOW          "z_basic_func_flow.txt"
#define FILE_NAME__OUT_TEMP                 "z_out.tmp"
//=========================================================
#if 0
    #include <windows.h>
    #define my_time_t                                       DWORD
    #define get_start_time(pTime_start)                     do{ (*pTime_start) = GetTickCount(); }while(0)
    #define get_duration_ms(pTime_start, pDiff_ms)          do{ (*pDiff_ms) = 0; (*pDiff_ms) = GetTickCount() - (*pTime_start); }while(0)
#else
    #include <time.h>
    #define my_time_t                                       time_t
    #define get_start_time(pTime_start)                     do{ (*pTime_start) = time(NULL); }while(0)
    #define get_duration_ms(pTime_start, pDiff_ms)          do{ (*pDiff_ms) = 0; (*pDiff_ms) = (time(NULL) - (*pTime_start)) * 1000; }while(0)
#endif

#define MESURE_TIME(pTime_start, pDiff_ms, is_start)                               \
        do{ if( !is_start ) {                                                      \
                get_duration_ms(pTime_start, pDiff_ms);                            \
                err_msg("------------------- duration '%ld' ms\n", *pDiff_ms);     \
            }                                                                      \
            get_start_time(pTime_start);                                           \
        }while(0)

//=========================================================

//==============================================================
static int
_create_reader(
    partial_read_t  *pHReader,
    const char      *pPath)
{
    int     rval = 0;

    do {
        if( !pPath )
        {
            err_msg("%s", "null path \n");
            rval = -1;
            break;
        }

        if( !(pHReader->fp = fopen(pPath, "rb")) )
        {
            err_msg("open '%s' fail \n", pPath);
            rval = -1;
            break;
        }

        pHReader->buf_size = MAX_BUFFER_SIZE;
        if( !(pHReader->pBuf = malloc(pHReader->buf_size)) )
        {
            err_msg("malloc '%ld' fail \n", pHReader->buf_size);
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
_duplicate_file(
    const char    *pIn_file_path,
    const char    *pOut_file_path)
{
    int             rval = 0;
    FILE            *fin = 0, *fout = 0;
    unsigned char   *pBuf = 0;

    do {
        long        buf_size = 1 << 20;
        long        file_size = 0l;

        if( !(pBuf = malloc(buf_size)) )
        {
            rval = -1;
            err_msg("malloc %ld fail \n", buf_size);
            break;
        }

        if( !(fin = fopen(pIn_file_path, "rb")) )
        {
            rval = -1;
            err_msg("open '%s' fail \n", pIn_file_path);
            break;
        }

        fseek(fin, 0l, SEEK_END);
        file_size = ftell(fin);
        fseek(fin, 0l, SEEK_SET);

        if( !(fout = fopen(pOut_file_path, "wb")) )
        {
            rval = -1;
            err_msg("open '%s' fail \n", pOut_file_path);
            break;
        }

        while( file_size )
        {
            long    nbytes = 0;

            nbytes = fread(pBuf, 1, buf_size, fin);
            if( nbytes )
                fwrite(pBuf, 1, nbytes, fout);

            file_size -= nbytes;
        }
    } while(0);

    if( pBuf )      free(pBuf);
    if( fin )       fclose(fin);
    if( fout )      fclose(fout);

    return rval;
}

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
_output_lds(
    partial_read_t  *pHReader_pattern,
    out_info_t      *pOut_info,
    char            *pTarget_tag,
    char            *pOut_name)
{
    int         rval = 0;
    FILE        *fout = 0;

    if( !(fout = fopen(pOut_name, "wb")) )
    {
        rval = -1;
        err_msg("open '%s' fail \n", pOut_name);
        return rval;
    }

    partial_read__full_buf(pHReader_pattern, _post_read);
    while( pHReader_pattern->pCur < pHReader_pattern->pEnd )
    {
        if( partial_read__full_buf(pHReader_pattern, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            char            *pAct_str = 0;
            char            *pTmp_str = 0;

            pAct_str = (char*)pHReader_pattern->pCur;

            pHReader_pattern->pCur += (strlen((char*)pHReader_pattern->pCur) + 1);

            pTmp_str = strstr(pAct_str, pTarget_tag);
            if( !pTmp_str )
            {
                fprintf(fout, "%s\n", pAct_str);
                continue;
            }

            {   // write out
                symbol_itm_t    *pCur = 0;

                pCur = pOut_info->pSymbol_table_finial->pSymbol_head;
                while( pCur )
                {
                    if( !pCur->is_outputted )
                    {
                        fprintf(fout, "\t\t* (.text.%s*)\n", pCur->symbol_name);
                        pCur->is_outputted = 1;
                    }

                    pCur = pCur->next;
                }

                pCur = pOut_info->pSymbol_table_leaf->pSymbol_head;
                while( pCur )
                {
                    if( !pCur->is_outputted )
                    {
                        lib_itm_t       *pCur_lib = pOut_info->pLib_table->pLib_head;
                        while( pCur_lib )
                        {
                            if( !pCur_lib->is_outputted &&
                                pCur_lib->crc_id == pCur->lib_crc_id )
                            {
                                obj_itm_t       *pCur_obj = pCur_lib->pObj_head;

                                fprintf(fout, "\n\t\t/* %s */\n", pCur_lib->lib_name);
                                while( pCur_obj )
                                {
                                    fprintf(fout, "\t\t*%s* (.text* )\n", pCur_obj->obj_name);
                                    pCur_obj = pCur_obj->next;
                                }

                                pCur_lib->is_outputted = 1;
                            }
                            pCur_lib = pCur_lib->next;
                        }

                        pCur->is_outputted = 1;
                    }

                    pCur = pCur->next;
                }
            }
        }

    }

    if( fout )      fclose(fout);

    return rval;
}


static int
_addr_to_func(
    partial_read_t  *pHReader_addr,
    table_symbols_t *pSymbol_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    char        *pOut_name = FILE_NAME__BASIC_FUNC_FLOW;

    if( !(fout = fopen(pOut_name, "wb")) )
    {
        err_msg("can't open '%s' \n", pOut_name);
    }

    partial_read__full_buf(pHReader_addr, NULL);
    while( pHReader_addr->pCur < pHReader_addr->pEnd )
    {
        unsigned int        is_found = 0;
        unsigned long       *pCur_func_addr = 0;
        symbol_itm_t        *pCur_item = 0;

        if( partial_read__full_buf(pHReader_addr, NULL) )
        {
            break;
        }

        pCur_func_addr = (unsigned long*)pHReader_addr->pCur;

        pCur_item = pSymbol_table->pSymbol_head;

        while( pCur_item )
        {
            // fprintf(stderr, "\t[%08lx] %s(%08x)\n", *pCur_func_addr, pCur_item->symbol_name, pCur_item->pAddr);
            if( (unsigned long)pCur_item->pAddr == *pCur_func_addr )
            {
                if( fout )
                    fprintf(fout, "* (.text.%s)\n", pCur_item->symbol_name);

                is_found = 1;
                break;
            }
            pCur_item = pCur_item->next;
        }

        if( !is_found )     err_msg("func addr 'x%p' can't find\n", pCur_func_addr);

        pHReader_addr->pCur += sizeof(unsigned long);
    }

    if( fout )  fclose(fout);

    return rval;
}

static FILE *g_fFunc_tree = 0;
static int  g_indent = 0;
static int
_look_up_relation(
    table_call_graph_t  *pCall_graph_table,
    uint32_t            crc_id,
    char                *pSym_name,
    table_symbols_t     *pSymbol_table_lite,
    table_symbols_t     *pSymbol_table_leaf)
{
    int     rval = 0;

    {
        ++g_indent;
        if( g_fFunc_tree )
        {
            int     i;
            for(i = 0; i < g_indent; ++i)
                fprintf(g_fFunc_tree, "    ");
            fprintf(g_fFunc_tree, " %s\n", pSym_name);
        }
    }

    do {
        symbol_relation_t       *pCur = 0, *pAct = 0;
        symbol_itm_t            *pSymbol_callee = 0;

        if( !pCall_graph_table->pSymbol_head )      break;

        pCur = pCall_graph_table->pSymbol_head;
        while( pCur )
        {
            if( pCur->crc_id == crc_id )
            {
                pAct = pCur;
                break;
            }
            pCur = pCur->next;
        }

        if( !pAct )
        {
            symbol_itm_t        *pSymbol_act = 0;

            // recode the symbol which the leaf function in graph relation
            if( pSymbol_table_leaf->pSymbol_head )
            {
                symbol_itm_t    *pSymbol_cur = pSymbol_table_leaf->pSymbol_head;

                while( pSymbol_cur )
                {
                    if( pSymbol_cur->crc_id == crc_id )
                    {
                        pSymbol_act = pSymbol_cur;
                        break;
                    }
                    pSymbol_cur = pSymbol_cur->next;
                }
            }

            if( pSymbol_act )       break;

            if( !(pSymbol_act = malloc(sizeof(symbol_itm_t))) )
            {
                err_msg("malloc '%d' fail \n", sizeof(symbol_itm_t));
                break;
            }
            memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

            pSymbol_act->crc_id = crc_id;
            snprintf(pSymbol_act->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", pSym_name);

            if( pSymbol_table_leaf->pSymbol_head )
            {
                pSymbol_table_leaf->pSymbol_cur->next = pSymbol_act;
                pSymbol_table_leaf->pSymbol_cur       = pSymbol_act;
            }
            else
            {
                pSymbol_table_leaf->pSymbol_head = pSymbol_table_leaf->pSymbol_cur = pSymbol_act;
            }

            break;
        }

        pSymbol_callee = pAct->pCallee_list_head;
        while( pSymbol_callee )
        {
            symbol_itm_t        *pSymbol_act = 0;

            // exist or not in symbol_table_lite
            if( pSymbol_table_lite->pSymbol_head )
            {
                symbol_itm_t    *pSymbol_cur = pSymbol_table_lite->pSymbol_head;

                while( pSymbol_cur )
                {
                    if( pSymbol_cur->crc_id == pSymbol_callee->crc_id )
                    {
                        pSymbol_act = pSymbol_cur;
                        break;
                    }
                    pSymbol_cur = pSymbol_cur->next;
                }
            }

            // add to symbol_table_lite
            if( !pSymbol_act )
            {
                if( !(pSymbol_act = malloc(sizeof(symbol_itm_t))) )
                {
                    err_msg("malloc '%d' fail \n", sizeof(symbol_itm_t));
                    break;
                }
                memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

                pSymbol_act->crc_id = pSymbol_callee->crc_id;
                snprintf(pSymbol_act->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", pSymbol_callee->symbol_name);

                if( pSymbol_table_lite->pSymbol_head )
                {
                    pSymbol_table_lite->pSymbol_cur->next = pSymbol_act;
                    pSymbol_table_lite->pSymbol_cur       = pSymbol_act;
                }
                else
                {
                    pSymbol_table_lite->pSymbol_head = pSymbol_table_lite->pSymbol_cur = pSymbol_act;
                }

                _look_up_relation(pCall_graph_table, pSymbol_callee->crc_id, pSymbol_callee->symbol_name,
                                  pSymbol_table_lite, pSymbol_table_leaf);
            }

            pSymbol_callee = pSymbol_callee->next;
        }

    } while(0);

    {
        --g_indent;
    }
    return rval;
}

static int
_complete_func_table(
    partial_read_t      *pHReader_basic_func_flow,
    table_call_graph_t  *pCall_graph_table,
    table_symbols_t      *pSymbol_table_lite,
    table_symbols_t      *pSymbol_table_leaf)
{
    int         rval = 0;
    regex_t     hRegex = {0};

    rval = regcomp(&hRegex, "^.*\\* \\(.text.(\\S+)\\)", REG_EXTENDED);

#if 0
    if( !(g_fFunc_tree = fopen("z_func_tree.txt", "wb")) )
    {
        err_msg("open '%s' fail \n", "func_tree.txt");
    }
#endif

    partial_read__full_buf(pHReader_basic_func_flow, _post_read);
    while( pHReader_basic_func_flow->pCur < pHReader_basic_func_flow->pEnd )
    {
        if( partial_read__full_buf(pHReader_basic_func_flow, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            uint32_t        crc_id = 0;
            char            *pAct_str = 0;
            char            symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            symbol_itm_t    *pSymbol_act = 0;
            size_t          nmatch = 4;
            regmatch_t      match_info[4] = {{0}};

            pAct_str = (char*)pHReader_basic_func_flow->pCur;

            pHReader_basic_func_flow->pCur += (strlen((char*)pHReader_basic_func_flow->pCur) + 1);

            rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
            if( rval == REG_NOMATCH || rval )
                continue;

            {
                if( match_info[1].rm_so != -1 )
                {
                    strncpy(symbol_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                    if( !strncmp(symbol_name, "startup.main", strlen("startup.main") + 1) )
                        snprintf(symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", "main");
                }
            }

            crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

            if( pSymbol_table_lite->pSymbol_head )
            {
                symbol_itm_t    *pSymbol_cur = pSymbol_table_lite->pSymbol_head;

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
                err_msg("malloc '%d' fail \n", sizeof(symbol_itm_t));
                break;
            }
            memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

            pSymbol_act->crc_id = crc_id;
            snprintf(pSymbol_act->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);
            if( pSymbol_table_lite->pSymbol_head )
            {
                pSymbol_table_lite->pSymbol_cur->next = pSymbol_act;
                pSymbol_table_lite->pSymbol_cur       = pSymbol_act;
            }
            else
            {
                pSymbol_table_lite->pSymbol_head = pSymbol_table_lite->pSymbol_cur = pSymbol_act;
            }

            // look up all_graph to get calllee
            _look_up_relation(pCall_graph_table, crc_id, symbol_name, pSymbol_table_lite, pSymbol_table_leaf);
        }
    }

    regfree(&hRegex);

    if( g_fFunc_tree )      fclose(g_fFunc_tree);

    return rval;
}

static int
_relate_leaf_with_lib(
    table_symbols_t  *pSymbol_table_leaf,
    table_symbols_t  *pSymbol_table_lib)
{
    int         rval = 0;

    do {
        symbol_itm_t    *pAct_symbol = 0;

        if( !pSymbol_table_leaf || !pSymbol_table_leaf->pSymbol_head ||
            !pSymbol_table_lib || !pSymbol_table_lib->pSymbol_head )
            break;

        pAct_symbol = pSymbol_table_leaf->pSymbol_head;
        while( pAct_symbol )
        {
            unsigned long   is_exist = 0;
            symbol_itm_t    *pCur_symbol = pSymbol_table_lib->pSymbol_head;
            while( pCur_symbol )
            {
                if( pAct_symbol->crc_id == pCur_symbol->crc_id )
                {
                    pAct_symbol->lib_crc_id = pCur_symbol->lib_crc_id;
                    pAct_symbol->obj_crc_id = pCur_symbol->obj_crc_id;
                    is_exist = 1;
                    break;
                }
                pCur_symbol = pCur_symbol->next;
            }

            if( !is_exist )
                err_msg("leaf symbol '%s' can't find lib\n", pAct_symbol->symbol_name);

            pAct_symbol = pAct_symbol->next;
        }
    } while(0);

    return rval;
}

static int
_gen_lds(char *pIni_path)
{
    int                 rval = 0;
    dictionary          *pIni = 0;
    const char          *pPath = 0;
    const char          *pOut_path = 0, *pTag_name = 0;
    table_mgr_t         *pTab_mgr = 0;
    table_op_args_t     args = {0};
    partial_read_t      hReader_symbol_nm = {0};
    partial_read_t      hReader_map = {0};
    partial_read_t      hReader_expand = {0};
    partial_read_t      hReader_func_addr = {0};
    partial_read_t      hReader_basic_func_flow = {0};
    partial_read_t      hReader_ld_pattern = {0};
    table_symbols_t     symbol_table_all = {0};
    table_call_graph_t  call_graph_table = {0};
    table_lib_t         lib_table = {0};
    table_symbols_t     symbol_lib_table = {0};
    table_symbols_t     symbol_table_lite = {0};
    table_symbols_t     symbol_leaf_table = {0};
    my_time_t           t_start = 0, t_diff = 0;

    do {
        int         i, tag_cnt = 0;

        pIni = iniparser_load(pIni_path);
        if( pIni == NULL )
        {
            err_msg("cannot parse file: '%s'\n", pIni_path);
            rval = -1;
            break;
        }

        // iniparser_dump(pIni, stderr);

        table_mgr__init(&pTab_mgr);
        //--------------------------------
        // generate all symbols database
        pPath = iniparser_getstring(pIni, "func_addr:symbol_table_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err_msg("%s", "no symbol_table_path\n");
            break;
        }

        MESURE_TIME(&t_start, &t_diff, 1);

        hReader_symbol_nm.alignment     = 0;
        hReader_symbol_nm.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_symbol_nm, pPath)) )
            break;

        args.pTunnel_info           = (void*)&hReader_symbol_nm;
        args.table.pTable_symbols   = &symbol_table_all;
        table_mgr__create_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

        args.pOut_name = "z_nm_symbol_list.txt";
        table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

        _destroy_reader(&hReader_symbol_nm);

        MESURE_TIME(&t_start, &t_diff, 0);

        //-------------------------------
        // extract static lib
        pPath = iniparser_getstring(pIni, "gcc:map_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err_msg("%s", "no map_file_path\n");
            break;
        }
        hReader_map.alignment     = 0;
        hReader_map.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_map, pPath)) )
            break;

        args.pTunnel_info       = (void*)&hReader_map;
        args.table.pTable_lib   = &lib_table;
        table_mgr__create_table(pTab_mgr, TABLE_ID_LIB_OBJ, &args);

        args.pOut_name = "z_lib_obj_list.txt";
        table_mgr__dump_table(pTab_mgr, TABLE_ID_LIB_OBJ, &args);

        MESURE_TIME(&t_start, &t_diff, 0);

        hReader_map.is_restart = 1;

        args.pTunnel_info           = (void*)&hReader_map;
        args.table.pTable_symbols   = &symbol_lib_table;
        table_mgr__create_table(pTab_mgr, TABLE_ID_SYMBOL_WITH_LIB_OBJ, &args);

        args.pOut_name = "z_symbol_lib.txt";
        table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOL_WITH_LIB_OBJ, &args);

        MESURE_TIME(&t_start, &t_diff, 0);

        _destroy_reader(&hReader_map);

        //--------------------------------
        // generate call graph relation
        pPath = iniparser_getstring(pIni, "gcc:expent_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err_msg("%s", "no expent_file_path\n");
            break;
        }
        hReader_expand.alignment     = 0;
        hReader_expand.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_expand, pPath)) )
            break;

        args.pTunnel_info               = (void*)&hReader_expand;
        args.table.pTable_call_graph    = &call_graph_table;
        table_mgr__create_table(pTab_mgr, TABLE_ID_CALL_GRAPH, &args);

        {
            table_op_args_t     sub_args = {0};
            table_symbols_t     leaf_func_table = {0};
            char                path_leaf_list[128] = {0};

            sub_args.table.pTable_symbols = &leaf_func_table;

            args.pOut_name    = "z_call_graph_list.txt";
            args.pTunnel_info = (void*)&sub_args;
            printf("==== %p, %p\n", args.pTunnel_info, sub_args.table.pTable_symbols);
            table_mgr__dump_table(pTab_mgr, TABLE_ID_CALL_GRAPH, &args);

            snprintf(path_leaf_list, 128, "z_all_leaf_%s", args.pOut_name);
            sub_args.pOut_name = path_leaf_list;
            table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOLS, &sub_args);

            table_mgr__destroy_table(pTab_mgr, TABLE_ID_SYMBOLS, &sub_args);
        }

        _destroy_reader(&hReader_expand);

        MESURE_TIME(&t_start, &t_diff, 0);

        //-------------------------------
        // duplicate file
        tag_cnt = iniparser_getint(pIni, "ld:region_tag_num", 0);
        pPath = iniparser_getstring(pIni, "ld:pattern_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err_msg("%s", "no pattern_file_path\n");
            break;
        }
        _duplicate_file(pPath, FILE_NAME__OUT_TEMP);

        pOut_path = iniparser_getstring(pIni, "ld:output_lds_file_path", NULL);
        if( !pOut_path )
        {
            rval = -1;
            err_msg("%s", "no output_lds_file_path\n");
            break;
        }

        for(i = 0; i < tag_cnt; ++i)
        {
            out_info_t      out_info = {0};
            char            tmp_str[128] = {0};

            MESURE_TIME(&t_start, &t_diff, 1);

            //--------------------------------
            // binary address file to generate basic function flow
            snprintf(tmp_str, 128, "func_addr:bin_file_path_%d", i);
            pPath = iniparser_getstring(pIni, tmp_str, NULL);
            if( !pPath )
            {
                rval = -1;
                err_msg("no '%s'\n", tmp_str);
                break;
            }

            hReader_func_addr.alignment     = iniparser_getint(pIni, "func_addr:addr_alignment", 0);
            hReader_func_addr.is_big_endian = iniparser_getboolean(pIni, "func_addr:is_big_endian", 0);
            if( (rval = _create_reader(&hReader_func_addr, pPath)) )
                break;

            _addr_to_func(&hReader_func_addr, &symbol_table_all);
            _destroy_reader(&hReader_func_addr);

            MESURE_TIME(&t_start, &t_diff, 0);

            //-------------------------------
            // complete functions base on basic_func_flow
            pPath = "func_list.dump"; //FILE_NAME__BASIC_FUNC_FLOW;
            hReader_basic_func_flow.alignment     = 0;
            hReader_basic_func_flow.is_big_endian = 0;
            if( (rval = _create_reader(&hReader_basic_func_flow, pPath)) )
                break;

            _complete_func_table(&hReader_basic_func_flow, &call_graph_table, &symbol_table_lite, &symbol_leaf_table);
            _destroy_reader(&hReader_basic_func_flow);

            args.table.pTable_symbols   = &symbol_table_lite;
            args.pOut_name              = "z_final_symbol_list.txt";
            table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

            args.table.pTable_symbols   = &symbol_leaf_table;
            args.pOut_name              = "z_symbol_leaf.txt";
            table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

            MESURE_TIME(&t_start, &t_diff, 0);

            //------------------------------
            // check a leaf symbol is in which lib
            _relate_leaf_with_lib(&symbol_leaf_table, &symbol_lib_table);

            args.table.pTable_symbols   = &symbol_leaf_table;
            args.pOut_name              = "z_leaf_lib.txt";
            table_mgr__dump_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

            MESURE_TIME(&t_start, &t_diff, 0);

            //-----------------------------------
            // output lds
            hReader_ld_pattern.alignment     = 0;
            hReader_ld_pattern.is_big_endian = 0;
            if( (rval = _create_reader(&hReader_ld_pattern, FILE_NAME__OUT_TEMP)) )
                break;

            snprintf(tmp_str, 128, "ld:region_tag_%d", i);
            pTag_name = iniparser_getstring(pIni, tmp_str, NULL);
            if( !pTag_name )
            {
                rval = -1;
                err_msg("no item '%s'\n", tmp_str);
                break;
            }

            out_info.pSymbol_table_finial = &symbol_table_lite;
            out_info.pSymbol_table_leaf   = &symbol_leaf_table;
            out_info.pLib_table           = &lib_table;
            _output_lds(&hReader_ld_pattern, &out_info, (char*)pTag_name, (char*)pOut_path);

            _duplicate_file(pOut_path, FILE_NAME__OUT_TEMP);

            MESURE_TIME(&t_start, &t_diff, 0);
        }

    } while(0);

    if( pIni )      iniparser_freedict(pIni);

    args.table.pTable_symbols   = &symbol_table_all;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

    args.table.pTable_symbols   = &symbol_table_lite;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

    args.table.pTable_symbols   = &symbol_leaf_table;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_SYMBOLS, &args);

    args.table.pTable_lib   = &lib_table;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_LIB_OBJ, &args);

    args.table.pTable_symbols   = &symbol_lib_table;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_SYMBOL_WITH_LIB_OBJ, &args);

    args.table.pTable_call_graph    = &call_graph_table;
    table_mgr__destroy_table(pTab_mgr, TABLE_ID_CALL_GRAPH, &args);

    table_mgr__deinit(&pTab_mgr);

    return rval;
}

//==============================================================
int main(int argc, char **argv)
{
    int     rval = 0;

    argv++; argc--;
    while( argc )
    {
        if( !strcmp(argv[0], "--cmp") )
        {
            extern int cmp_symbol(char *pList_1, char *pList_2);
            rval = cmp_symbol(argv[1], argv[2]);
            argv += 2; argc -= 2;
        }
        else if( !strcmp(argv[0], "--rsize") )
        {

        }
        else if( !strcmp(argv[0], "--glds") )
        {
            _gen_lds(argv[1]);
            argv++; argc--;
        }
        argv++; argc--;
    }

    return rval;
}
