
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iniparser.h"
#include "partial_read.h"
#include "symbol_info.h"
#include "regex.h"
#include "crc32.h"

//#include "mleak_check.h"
//===================================================
#define MAX_BUFFER_SIZE                     (2 << 20)
#define FILE_NAME__BASIC_FUNC_FLOW          "z_basic_func_flow.txt"
#define FILE_NAME__OUT_TEMP                 "z_out.tmp"

//===================================================
#define err(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)

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
                err("------------------- duration '%ld' ms\n", *pDiff_ms);         \
            }                                                                      \
            get_start_time(pTime_start);                                           \
        }while(0)



//===================================================
/**
 *  symbol table
 */
typedef struct symbol_table
{
    symbol_itm_t        *pSymbol_head;
    symbol_itm_t        *pSymbol_cur;

} symbol_table_t;


/**
 *  call graph table
 */
typedef struct call_graph_table
{
    symbol_relation_t        *pSymbol_head;
    symbol_relation_t        *pSymbol_cur;

} call_graph_table_t;

/**
 *  static lib table
 */
typedef struct lib_table
{
    lib_itm_t       *pLib_head;
    lib_itm_t       *pLib_cur;

} lib_table_t;

/**
 *  all info for output
 */
typedef struct out_info
{
    symbol_table_t      *pSymbol_table_finial;
    symbol_table_t      *pSymbol_table_leaf;
    lib_table_t         *pLib_table;

} out_info_t;
//===================================================
//===================================================
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
        if( pBuf[i] == '\n' || pBuf[i] == '\r' )
            pBuf[i] = '\0';
    }
    return 0;
}

static int
_create_symbol_table(
    partial_read_t  *pHReader_db,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    regex_t     hRegex = {0};

    regcomp(&hRegex, "(.*)\\s+(\\w)\\s+(.*)\\s+.*$", REG_EXTENDED);

    // generate symbol database
    partial_read__full_buf(pHReader_db, _post_read);
    while( pHReader_db->pCur < pHReader_db->pEnd )
    {
        if( partial_read__full_buf(pHReader_db, _post_read) )
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

            pAct_str = (char*)pHReader_db->pCur;

            pHReader_db->pCur += (strlen((char*)pHReader_db->pCur) + 1);

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
                        // err("get the same symbol name '%s'\n", pCur_item->symbol_name);
                        is_dummy = 1;
                        break;
                    }
                    pCur_item = pCur_item->next;
                }

                if( is_dummy )      continue;

                if( !(pNew_item = malloc(sizeof(symbol_itm_t))) )
                {
                    err("malloc symbol item (%d) fail \n", sizeof(symbol_itm_t));
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
            char    lib_crc_str[64] = {0};
            snprintf(lib_crc_str, 64, "# lib_crc= x%08x", pCur->lib_crc_id);
            fprintf(fout, "* (.text.%s*) %s\n", pCur->symbol_name, (pCur->lib_crc_id) ? lib_crc_str : "");
            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}

static int
_addr_to_func(
    partial_read_t  *pHReader_addr,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    char        *pOut_name = FILE_NAME__BASIC_FUNC_FLOW;

    if( !(fout = fopen(pOut_name, "wb")) )
    {
        err("can't open '%s' \n", pOut_name);
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

        if( !is_found )     err("func addr 'x%p' can't find\n", pCur_func_addr);

        pHReader_addr->pCur += sizeof(unsigned long);
    }

    if( fout )  fclose(fout);

    return rval;
}

static int
_create_call_graph_table(
    partial_read_t      *pHReader_expand,
    call_graph_table_t  *pCall_graph_table)
{
    int                 rval = 0;
    symbol_relation_t   *pCur_item = 0;
    regex_t             hRegex = {0};
    regex_t             hRegex_sub = {0};

    regcomp(&hRegex, "^;; Function (\\S+) \\((\\S+), .*\\)", REG_EXTENDED);
    regcomp(&hRegex_sub, ".*\\(call .*\"(.*)\".*\\)", REG_EXTENDED);

    partial_read__full_buf(pHReader_expand, _post_read);
    while( pHReader_expand->pCur < pHReader_expand->pEnd )
    {
        if( partial_read__full_buf(pHReader_expand, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            char                *pAct_str = 0;
            char                symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            size_t              nmatch = 4;
            regmatch_t          match_info[4] = {{0}};

            pAct_str = (char*)pHReader_expand->pCur;

            pHReader_expand->pCur += (strlen((char*)pHReader_expand->pCur) + 1);

            if( *pAct_str == '\0' )     continue;

            rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
            if( !rval )
            {
                if( match_info[1].rm_so != -1 )
                {
                    uint32_t        crc_id = 0;

                    strncpy(symbol_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);

                    crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

                    //--------------------------
                    // check exist or not
                    pCur_item = 0;
                    if( pCall_graph_table->pSymbol_head )
                    {
                        symbol_relation_t       *pCur = pCall_graph_table->pSymbol_head;
                        while( pCur )
                        {
                            if( pCur->crc_id == crc_id )
                            {
                                pCur_item = pCur;
                                break;
                            }
                            pCur = pCur->next;
                        }

                        if( pCur_item )     continue;
                    }

                    //---------------------------
                    // malloc new item
                    if( !(pCur_item = malloc(sizeof(symbol_relation_t))) )
                    {
                        err("malloc '%d' fail \n", sizeof(symbol_relation_t));
                        break;
                    }
                    memset(pCur_item, 0x0, sizeof(symbol_relation_t));

                    pCur_item->crc_id = crc_id;
                    snprintf(pCur_item->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);

                    // insert to list
                    if( pCall_graph_table->pSymbol_head )
                    {
                        pCall_graph_table->pSymbol_cur->next = pCur_item;
                        pCall_graph_table->pSymbol_cur       = pCur_item;
                    }
                    else
                    {
                        pCall_graph_table->pSymbol_head = pCall_graph_table->pSymbol_cur = pCur_item;
                    }
                }

                continue;
            }

            rval = regexec(&hRegex_sub, pAct_str, nmatch, match_info, 0);
            if( !rval )
            {
                if( match_info[1].rm_so != -1 )
                {
                    uint32_t        crc_id = 0;
                    symbol_itm_t    *pNew_callee_symbol = 0;

                    strncpy(symbol_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);

                    if( !pCur_item )
                    {
                        err("%s", "lose current function name\n");
                        break;
                    }

                    // check exist or not
                    if( pCur_item->pCallee_list_head )
                    {
                        unsigned int            is_dummy = 0;
                        symbol_itm_t            *pCur_callee = 0;

                        crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

                        pCur_callee = pCur_item->pCallee_list_head;
                        while( pCur_callee )
                        {
                            if( (is_dummy = (pCur_callee->crc_id == crc_id)) )
                                break;

                            pCur_callee = pCur_callee->next;
                        }

                        if( is_dummy )      continue;
                    }

                    // create caller symbol item
                    if( !(pNew_callee_symbol = malloc(sizeof(symbol_itm_t))) )
                    {
                        err("malloc '%d' fail \n", sizeof(symbol_itm_t));
                        break;
                    }
                    memset(pNew_callee_symbol, 0x0, sizeof(symbol_itm_t));

                    pNew_callee_symbol->crc_id = (crc_id) ? crc_id : calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));
                    snprintf(pNew_callee_symbol->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);

                    // insert to list
                    if( pCur_item->pCallee_list_head )
                    {
                        pCur_item->pCallee_list_cur->next = pNew_callee_symbol;
                        pCur_item->pCallee_list_cur       = pNew_callee_symbol;
                    }
                    else
                    {
                        pCur_item->pCallee_list_head = pCur_item->pCallee_list_cur = pNew_callee_symbol;
                    }
                }

                continue;
            }
        }
    }

    regfree(&hRegex);
    regfree(&hRegex_sub);

    {   // find the leaf function
        symbol_relation_t       *pCur_caller = pCall_graph_table->pSymbol_head;
        while( pCur_caller )
        {
            symbol_itm_t    *pCallee_symbol = pCur_caller->pCallee_list_head;
            while( pCallee_symbol )
            {
                unsigned int            is_leaf = 1;
                symbol_relation_t       *pTmp_caller = pCall_graph_table->pSymbol_head;
                while( pTmp_caller )
                {
                    if( pCallee_symbol->crc_id == pTmp_caller->crc_id )
                    {
                        is_leaf = 0;
                        break;
                    }

                    pTmp_caller = pTmp_caller->next;
                }

                pCallee_symbol->is_leaf = is_leaf;

                pCallee_symbol = pCallee_symbol->next;
            }

            pCur_caller = pCur_caller->next;
        }
    }

    return rval;
}

static int
_destory_call_graph_table(
    call_graph_table_t  *pCall_graph_table)
{
    int         rval = 0;

    do {
        symbol_relation_t      *pCur = 0;
        symbol_itm_t           *pCur_callee = 0;

        if( !pCall_graph_table)     break;

        pCur = pCall_graph_table->pSymbol_head;
        while( pCur )
        {
            symbol_relation_t      *pTmp = pCur;

            pCur_callee = pCur->pCallee_list_head;
            while( pCur_callee )
            {
                symbol_itm_t    *pTmp_callee = pCur_callee;

                pCur_callee = pCur_callee->next;
                free(pTmp_callee);
            }

            pCur = pCur->next;
            free(pTmp);
        }

        memset(pCall_graph_table, 0x0, sizeof(call_graph_table_t));
    } while(0);

    return rval;
}

static int
_dump_call_graph_table(
    char                *pOut_name,
    call_graph_table_t  *pCall_graph_table)
{
    int         rval = 0;
    FILE        *fout = 0;

    do {
        symbol_relation_t      *pCur = 0;
        symbol_itm_t           *pCur_callee = 0;

        if( !pCall_graph_table)     break;

        if( !(fout = fopen(pOut_name, "wb")) )
        {
            err("open '%s' fail \n", pOut_name);
            break;
        }

        pCur = pCall_graph_table->pSymbol_head;
        while( pCur )
        {
            pCur_callee = pCur->pCallee_list_head;
            while( pCur_callee )
            {
                fprintf(fout, "'%s' -> '%s'\n", pCur->symbol_name, pCur_callee->symbol_name);
                pCur_callee = pCur_callee->next;
            }

            pCur = pCur->next;
        }

        {   // dump all leaf functions
            symbol_table_t      leaf_func_table = {0};
            char                path_leaf_list[128] = {0};

            snprintf(path_leaf_list, 128, "z_all_leaf_%s", pOut_name);

            pCur = pCall_graph_table->pSymbol_head;
            while( pCur )
            {
                pCur_callee = pCur->pCallee_list_head;
                while( pCur_callee )
                {
                    if( pCur_callee->is_leaf )
                    {
                        symbol_itm_t           *pAct_callee = 0;

                        if( leaf_func_table.pSymbol_head )
                        {
                            symbol_itm_t    *pCur_leaf = leaf_func_table.pSymbol_head;
                            while( pCur_leaf )
                            {
                                if( pCur_callee->crc_id == pCur_leaf->crc_id )
                                {
                                    pAct_callee = pCur_callee;
                                    break;
                                }
                                pCur_leaf = pCur_leaf->next;
                            }
                        }

                        if( !pAct_callee )
                        {
                            if( !(pAct_callee = malloc(sizeof(symbol_itm_t))) )
                            {
                                err("malloc %d fail \n", sizeof(symbol_itm_t));
                                break;
                            }

                            memcpy(pAct_callee, pCur_callee, sizeof(symbol_itm_t));
                            pAct_callee->next = 0;

                            if( leaf_func_table.pSymbol_head )
                            {
                                leaf_func_table.pSymbol_cur->next = pAct_callee;
                                leaf_func_table.pSymbol_cur       = pAct_callee;
                            }
                            else
                            {
                                leaf_func_table.pSymbol_head = leaf_func_table.pSymbol_cur = pAct_callee;
                            }
                        }
                    }

                    pCur_callee = pCur_callee->next;
                }

                pCur = pCur->next;
            }

            _dump_symbol_table(path_leaf_list, &leaf_func_table);
            _destroy_symbol_table(&leaf_func_table);
        }

    } while(0);

    if( fout )          fclose(fout);

    return rval;
}

static int
_create_lib_obj_table(
    partial_read_t      *pHReader_map,
    lib_table_t         *pLib_table)
{
    int                 rval = 0;
    regex_t             hRegex = {0};

    rval = regcomp(&hRegex, ".*/(.*)\\.(.*)\\((.*)\\.(.*)\\)", REG_EXTENDED);

    partial_read__full_buf(pHReader_map, _post_read);
    while( pHReader_map->pCur < pHReader_map->pEnd )
    {
        if( partial_read__full_buf(pHReader_map, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            int                 i;
            uint32_t            crc_id = 0;
            char                *pAct_str = 0;
            char                lib_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            char                obj_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            lib_itm_t           *pAct_lib = 0;
            obj_itm_t           *pAct_obj = 0;
            size_t              nmatch = 5;
            regmatch_t          match_info[5] = {{0}};

            pAct_str = (char*)pHReader_map->pCur;

            pHReader_map->pCur += (strlen((char*)pHReader_map->pCur) + 1);

            for(i = 0; i < strlen(pAct_str); ++i)
                pAct_str[i] = (pAct_str[i] == '\\') ? '/' : pAct_str[i];

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
                        // err("wrong lib name '%s.%c'\n", lib_name, pAct_str[match_info[2].rm_so]);
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
                        // err("wrong obj name '%s.%c'\n", obj_name, pAct_str[match_info[4].rm_so]);
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
                    err("malloc '%d' fail \n", sizeof(lib_itm_t));
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
                err("malloc '%d' fail \n", sizeof(obj_itm_t));
                break;
            }
            memset(pAct_obj, 0x0, sizeof(obj_itm_t));

            pAct_obj->crc_id = crc_id;
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
_destory_lib_obj_table(
    lib_table_t         *pLib_table)
{
    int         rval = 0;

    do {
        lib_itm_t      *pCur = 0;
        obj_itm_t      *pCur_obj = 0;

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

        memset(pLib_table, 0x0, sizeof(lib_table_t));
    } while(0);

    return rval;
}

static int
_dump_lib_obj_table(
    char            *pOut_name,
    lib_table_t     *pLib_table)
{
    int         rval = 0;
    FILE        *fout = 0;

    do {
        lib_itm_t      *pCur = 0;
        obj_itm_t      *pCur_obj = 0;

        if( !pLib_table)     break;

        if( !(fout = fopen(pOut_name, "wb")) )
        {
            err("open '%s' fail \n", pOut_name);
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

static FILE *g_fFunc_tree = 0;
static int  g_indent = 0;
static int
_look_up_relation(
    call_graph_table_t  *pCall_graph_table,
    uint32_t            crc_id,
    char                *pSym_name,
    symbol_table_t      *pSymbol_table_lite,
    symbol_table_t      *pSymbol_table_leaf)
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
                err("malloc '%d' fail \n", sizeof(symbol_itm_t));
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
                    err("malloc '%d' fail \n", sizeof(symbol_itm_t));
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
    call_graph_table_t  *pCall_graph_table,
    symbol_table_t      *pSymbol_table_lite,
    symbol_table_t      *pSymbol_table_leaf)
{
    int         rval = 0;
    regex_t     hRegex = {0};

    rval = regcomp(&hRegex, "^.*\\* \\(.text.(\\S+)\\)", REG_EXTENDED);

#if 0
    if( !(g_fFunc_tree = fopen("z_func_tree.txt", "wb")) )
    {
        err("open '%s' fail \n", "func_tree.txt");
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
                err("malloc '%d' fail \n", sizeof(symbol_itm_t));
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
_create_symbol_table_with_lib_obj(
    partial_read_t  *pHReader_map,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    regex_t     hRegex = {0};
    regex_t     hRegex_sub = {0};
    regex_t     hRegex_1 = {0};
    regex_t     hRegex_sub_1 = {0};

    rval = regcomp(&hRegex, "^\\s*\\.text\\.(\\w+)\\b", REG_EXTENDED);

    rval = regcomp(&hRegex_sub, ".*0x[a-fA-F0-9]+\\s+0x[a-fA-F0-9]+\\s+.*\\/(.*)\\.(.*)\\((.*)\\.(.*)\\)", REG_EXTENDED);

    rval = regcomp(&hRegex_1, "^\\s*\\.text\\s+0x[a-fA-F0-9]+\\s+0x[a-fA-F0-9]+\\s+.*\\/(.*)\\.(.*)\\((.*)\\.(.*)\\)", REG_EXTENDED);

    rval = regcomp(&hRegex_sub_1, "^\\s*0x[a-fA-F0-9]+\\s+(\\w+)\\b", REG_EXTENDED);
    if( rval )
    {
        char    msgbuf[256] = {0};
        regerror(rval, &hRegex_1, msgbuf, sizeof(msgbuf));
        printf("%s\n", msgbuf);
    }

    partial_read__full_buf(pHReader_map, _post_read);
    while( pHReader_map->pCur < pHReader_map->pEnd )
    {
        if( partial_read__full_buf(pHReader_map, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            int             i, retry_cnt = 0;
            unsigned int    crc_id = 0;
            char            *pAct_str = 0;
            char            symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            char            lib_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            char            obj_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            size_t          nmatch = 6;
            regmatch_t      match_info[6] = {{0}};

            pAct_str = (char*)pHReader_map->pCur;

            pHReader_map->pCur += (strlen((char*)pHReader_map->pCur) + 1);

            for(i = 0; i < strlen(pAct_str); ++i)
                pAct_str[i] = (pAct_str[i] == '\\') ? '/' : pAct_str[i];

            /**
             *  case 0
             *   .text.SHELL_Read
             *          0x00062dbc       0x6c /output/lib/libshell.a(Shell_MemIO.o)
             *
             *  case 1
             *   .text.SHELL_Rd   0x00062ccc       0x6c /output/lib/libshell.a(Shell_MemIO.o)
             */
            rval = regexec(&hRegex, pAct_str, nmatch, match_info, 0);
            if( !rval )
            {
                if( match_info[1].rm_so == -1 )
                    continue;

                strncpy(symbol_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

                //  check symbol name exist or not
                if( pSymbol_table->pSymbol_head )
                {
                    unsigned long   is_dummy = 0;
                    symbol_itm_t    *pCur_item = 0;

                    pCur_item = pSymbol_table->pSymbol_head;
                    while( pCur_item )
                    {
                        if( pCur_item->crc_id == crc_id )
                        {
                            // err("get the same symbol name '%s'\n", pCur_item->symbol_name);
                            is_dummy = 1;
                            break;
                        }
                        pCur_item = pCur_item->next;
                    }

                    if( is_dummy )      continue;
                }

                retry_cnt = 0;
                do {
                    symbol_itm_t    *pSymbol_act = 0;

                    if( retry_cnt )
                    {
                        pAct_str = (char*)pHReader_map->pCur;

                        pHReader_map->pCur += (strlen((char*)pHReader_map->pCur) + 1);

                        for(i = 0; i < strlen(pAct_str); ++i)
                            pAct_str[i] = (pAct_str[i] == '\\') ? '/' : pAct_str[i];
                    }

                    rval = regexec(&hRegex_sub, pAct_str, nmatch, match_info, 0);
                    if( !rval )
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
                                err("wrong lib name '%s.%c'\n", lib_name, pAct_str[match_info[2].rm_so]);
                                break;
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
                                err("wrong obj name '%s.%c'\n", obj_name, pAct_str[match_info[4].rm_so]);
                                break;
                            }
                        }

                        // add to symbol table
                        if( !(pSymbol_act = malloc(sizeof(symbol_itm_t))) )
                        {
                            err("malloc '%d' fail \n", sizeof(symbol_itm_t));
                            break;
                        }
                        memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

                        pSymbol_act->crc_id     = crc_id;
                        pSymbol_act->lib_crc_id = calc_crc32((uint8_t*)lib_name, strlen(lib_name));
                        pSymbol_act->obj_crc_id = calc_crc32((uint8_t*)obj_name, strlen(obj_name));
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
                        break;
                    }

                } while( ++retry_cnt < 2 );

                continue;
            }

            /**
             * case 2
             *   .text  0x0028e688      0x278 /usr/local/gcc-arm-none-eabi-4_9/thumb/fpu/libgcc.a(_udivsi3.o)
             *          0x00028e688                __udivsi3
             */

            rval = regexec(&hRegex_1, pAct_str, nmatch, match_info, 0);
            if( !rval )
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
                        err("wrong lib name '%s.%c'\n", lib_name, pAct_str[match_info[2].rm_so]);
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
                        err("wrong obj name '%s.%c'\n", obj_name, pAct_str[match_info[4].rm_so]);
                        continue;
                    }
                }

                do {
                    symbol_itm_t    *pSymbol_act = 0;

                    pAct_str = (char*)pHReader_map->pCur;

                    for(i = 0; i < strlen(pAct_str); ++i)
                        pAct_str[i] = (pAct_str[i] == '\\') ? '/' : pAct_str[i];

                    rval = regexec(&hRegex_sub_1, pAct_str, nmatch, match_info, 0);
                    if( rval == REG_NOMATCH || rval )
                        break;

                    pHReader_map->pCur += (strlen((char*)pHReader_map->pCur) + 1);

                    if( match_info[1].rm_so == -1 )
                        continue;

                    memset(symbol_name, 0x0, MAX_SYMBOL_NAME_LENGTH);
                    strncpy(symbol_name, &pAct_str[match_info[1].rm_so], match_info[1].rm_eo - match_info[1].rm_so);
                    crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));

                    //  check symbol name exist or not
                    if( pSymbol_table->pSymbol_head )
                    {
                        unsigned long   is_dummy = 0;
                        symbol_itm_t    *pCur_item = 0;

                        pCur_item = pSymbol_table->pSymbol_head;
                        while( pCur_item )
                        {
                            if( pCur_item->crc_id == crc_id )
                            {
                                // err("get the same symbol name '%s'\n", pCur_item->symbol_name);
                                is_dummy = 1;
                                break;
                            }
                            pCur_item = pCur_item->next;
                        }

                        if( is_dummy )      continue;
                    }

                    // add to symbol table
                    if( !(pSymbol_act = malloc(sizeof(symbol_itm_t))) )
                    {
                        err("malloc '%d' fail \n", sizeof(symbol_itm_t));
                        break;
                    }
                    memset(pSymbol_act, 0x0, sizeof(symbol_itm_t));

                    pSymbol_act->crc_id     = crc_id;
                    pSymbol_act->lib_crc_id = calc_crc32((uint8_t*)lib_name, strlen(lib_name));
                    pSymbol_act->obj_crc_id = calc_crc32((uint8_t*)obj_name, strlen(obj_name));
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

                } while(1);

                continue;
            }
        }
    }

    regfree(&hRegex);
    regfree(&hRegex_sub);
    regfree(&hRegex_1);
    regfree(&hRegex_sub_1);

    return rval;
}

static int
_relate_leaf_with_lib(
    symbol_table_t  *pSymbol_table_leaf,
    symbol_table_t  *pSymbol_table_lib)
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
                err("leaf symbol '%s' can't find lib\n", pAct_symbol->symbol_name);

            pAct_symbol = pAct_symbol->next;
        }
    } while(0);

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
            err("malloc %ld fail \n", buf_size);
            break;
        }

        if( !(fin = fopen(pIn_file_path, "rb")) )
        {
            rval = -1;
            err("open '%s' fail \n", pIn_file_path);
            break;
        }

        fseek(fin, 0l, SEEK_END);
        file_size = ftell(fin);
        fseek(fin, 0l, SEEK_SET);

        if( !(fout = fopen(pOut_file_path, "wb")) )
        {
            rval = -1;
            err("open '%s' fail \n", pOut_file_path);
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
        err("open '%s' fail \n", pOut_name);
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

//===================================================
int main(int argc, char **argv)
{
    int                 rval = 0;
    dictionary          *pIni = 0;
    const char          *pPath = 0;
    const char          *pOut_path = 0, *pTag_name = 0;
    partial_read_t      hReader_func_addr = {0};
    partial_read_t      hReader_symbol_db = {0};
    partial_read_t      hReader_expand = {0};
    partial_read_t      hReader_map = {0};
    partial_read_t      hReader_basic_func_flow = {0};
    partial_read_t      hReader_ld_pattern = {0};
    symbol_table_t      symbol_table_all = {0};
    symbol_table_t      symbol_table_lite = {0};
    symbol_table_t      symbol_leaf_table = {0};
    symbol_table_t      symbol_lib_table = {0};
    call_graph_table_t  call_graph_table = {0};
    lib_table_t         lib_table = {0};
    my_time_t           t_start = 0, t_diff = 0;

    do {
        int         i, tag_cnt = 0;

        pIni = iniparser_load(argv[1]);
        if( pIni == NULL )
        {
            err("cannot parse file: '%s'\n", argv[1]);
            rval = -1;
            break;
        }

        // iniparser_dump(pIni, stderr);

        //--------------------------------
        // generate all symbols database
        pPath = iniparser_getstring(pIni, "func_addr:symbol_table_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no symbol_table_path\n");
            break;
        }

        MESURE_TIME(&t_start, &t_diff, 1);

        hReader_symbol_db.alignment     = 0;
        hReader_symbol_db.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_symbol_db, pPath)) )
            break;

        _create_symbol_table(&hReader_symbol_db, &symbol_table_all);
        _dump_symbol_table("z_nm_symbol_list.txt", &symbol_table_all);
        _destroy_reader(&hReader_symbol_db);

        MESURE_TIME(&t_start, &t_diff, 0);

        //-------------------------------
        // extract static lib
        pPath = iniparser_getstring(pIni, "gcc:map_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no map_file_path\n");
            break;
        }
        hReader_map.alignment     = 0;
        hReader_map.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_map, pPath)) )
            break;

        _create_lib_obj_table(&hReader_map, &lib_table);

        MESURE_TIME(&t_start, &t_diff, 0);

        hReader_map.is_restart = 1;
        _create_symbol_table_with_lib_obj(&hReader_map, &symbol_lib_table);

        MESURE_TIME(&t_start, &t_diff, 0);

        _dump_symbol_table("z_symbol_lib.txt", &symbol_lib_table);

        _destroy_reader(&hReader_map);

        _dump_lib_obj_table("z_lib_obj_list.txt", &lib_table);

        MESURE_TIME(&t_start, &t_diff, 0);

        //--------------------------------
        // generate call graph relation
        pPath = iniparser_getstring(pIni, "gcc:expent_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no expent_file_path\n");
            break;
        }
        hReader_expand.alignment     = 0;
        hReader_expand.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_expand, pPath)) )
            break;

        _create_call_graph_table(&hReader_expand, &call_graph_table);
        _destroy_reader(&hReader_expand);

        _dump_call_graph_table("z_call_graph_list.txt", &call_graph_table);

        MESURE_TIME(&t_start, &t_diff, 0);

        //-------------------------------
        // duplicate file
        tag_cnt = iniparser_getint(pIni, "ld:region_tag_num", 0);
        pPath = iniparser_getstring(pIni, "ld:pattern_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no pattern_file_path\n");
            break;
        }
        _duplicate_file(pPath, FILE_NAME__OUT_TEMP);

        pOut_path = iniparser_getstring(pIni, "ld:output_lds_file_path", NULL);
        if( !pOut_path )
        {
            rval = -1;
            err("%s", "no output_lds_file_path\n");
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
                err("no '%s'\n", tmp_str);
                break;
            }

            hReader_func_addr.alignment     = iniparser_getint(pIni, "func_addr:addr_alignment", 0);
            hReader_func_addr.is_big_endian = iniparser_getboolean(pIni, "func_addr:is_big_endian", 0);
            if( (rval = _create_reader(&hReader_func_addr, pPath)) )
                break;

            _addr_to_func(&hReader_func_addr, &symbol_table_all);
            _destroy_reader(&hReader_func_addr);

            _destroy_symbol_table(&symbol_table_all);

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

            _dump_symbol_table("z_final_symbol_list.txt", &symbol_table_lite);
            _dump_symbol_table("z_symbol_leaf.txt", &symbol_leaf_table);

            MESURE_TIME(&t_start, &t_diff, 0);

            //------------------------------
            // check a leaf symbol is in which lib
            _relate_leaf_with_lib(&symbol_leaf_table, &symbol_lib_table);
            _dump_symbol_table("z_leaf_lib.txt", &symbol_leaf_table);

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
                err("no item '%s'\n", tmp_str);
                break;
            }

            out_info.pSymbol_table_finial = &symbol_table_lite;
            out_info.pSymbol_table_leaf   = &symbol_leaf_table;
            out_info.pLib_table           = &lib_table;
            _output_lds(&hReader_ld_pattern, &out_info, (char*)pTag_name, (char*)pOut_path);

            _duplicate_file(pOut_path, FILE_NAME__OUT_TEMP);

            MESURE_TIME(&t_start, &t_diff, 0);
        }

    }while(0);

    MESURE_TIME(&t_start, &t_diff, 1);

    _destroy_symbol_table(&symbol_table_all);
    _destroy_symbol_table(&symbol_table_lite);
    _destroy_symbol_table(&symbol_leaf_table);
    _destroy_symbol_table(&symbol_lib_table);
    _destory_call_graph_table(&call_graph_table);
    _destory_lib_obj_table(&lib_table);
    _destroy_reader(&hReader_func_addr);
    _destroy_reader(&hReader_symbol_db);
    _destroy_reader(&hReader_expand);
    _destroy_reader(&hReader_map);
    _destroy_reader(&hReader_basic_func_flow);
    _destroy_reader(&hReader_ld_pattern);

    if( pIni )      iniparser_freedict(pIni);

    MESURE_TIME(&t_start, &t_diff, 0);

    //mlead_dump();
    return rval;
}
