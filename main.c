
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "iniparser.h"
#include "partial_read.h"
#include "symbol_info.h"
#include "crc32.h"

#include "mleak_check.h"
//===================================================
#define MAX_BUFFER_SIZE         (2 << 20)
//===================================================
#define err(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)


//===================================================
typedef struct symbol_table
{
    symbol_itm_t        *pSymbol_head;
    symbol_itm_t        *pSymbol_cur;

} symbol_table_t;

typedef struct call_graph_table
{
    symbol_relation_t        *pSymbol_head;
    symbol_relation_t        *pSymbol_cur;

} call_graph_table_t;

//===================================================
//===================================================
static int
_create_reader(
    partial_read_t  *pHReader,
    const char      *pPath)
{
    int     rval = 0;

    do {
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
    partial_read_t  *pHReader_db,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;

    // generate symbol database
    partial_read__full_buf(pHReader_db, _post_read);
    while( pHReader_db->pCur < pHReader_db->pEnd )
    {
        if( partial_read__full_buf(pHReader_db, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            unsigned long   int_addr = 0l;
            char            *pAct_str = 0;
            char            symbol_type = 0;
            char            symbol_name[256] = {0};

            pAct_str = (char*)pHReader_db->pCur;

            pHReader_db->pCur += (strlen((char*)pHReader_db->pCur) + 1);

            rval = sscanf(pAct_str, "%lx %c %s", &int_addr, &symbol_type, symbol_name);
            if( rval != 3 )
            {
                // err("sscanf '%s' fail (return %d)\n", pAct_str, rval);
                continue;
            }

            if( symbol_name[0] != '$' &&
                (symbol_type == 'T' || symbol_type == 't') )
            {
                symbol_itm_t    *pNew_item = 0;
                symbol_itm_t    *pCur_item = 0;

                if( !(pNew_item = malloc(sizeof(symbol_itm_t))) )
                {
                    err("malloc symbol item (%d) fail \n", sizeof(symbol_itm_t));
                    break;
                }
                memset(pNew_item, 0x0, sizeof(symbol_itm_t));

                pNew_item->pAddr  = (void*)int_addr;
                pNew_item->crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));
                snprintf(pNew_item->symbol_name, MAX_SYMBOL_NAME_LENGTH, "%s", symbol_name);

                // check the same symbol
                pCur_item = pSymbol_table->pSymbol_head;
                while( pCur_item )
                {
                    if( pCur_item->crc_id == pNew_item->crc_id )
                    {
                        // err("get the same symbol name '%s'\n", pCur_item->symbol_name);
                        break;
                    }
                    pCur_item = pCur_item->next;
                }

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

    return rval;
}

static int
_destroy_symbol_table(
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    do {
        symbol_itm_t    *pCur =0;

        if( !pSymbol_table )    break;

        pCur = pSymbol_table->pSymbol_head;
        while( pCur )
        {
            symbol_itm_t    *pTmp = pCur;

            pCur = pCur->next;
            free(pTmp);
        }
    } while(0);
    return rval;
}

static int
_addr_to_func(
    partial_read_t  *pHReader_addr,
    symbol_table_t  *pSymbol_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    char        *pOut_name = "basic_func_flow.txt";

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

    partial_read__full_buf(pHReader_expand, _post_read);
    while( pHReader_expand->pCur < pHReader_expand->pEnd )
    {
        if( partial_read__full_buf(pHReader_expand, _post_read) )
        {
            break;
        }

        {   // start parsing a line
            char                *pAct_str = 0;
            char                symbol_name[256] = {0};

            pAct_str = (char*)pHReader_expand->pCur;

            pHReader_expand->pCur += (strlen((char*)pHReader_expand->pCur) + 1);

            rval = sscanf(pAct_str, ";; Function %s (%*s)", symbol_name);
            if( rval == 1 )
            {
                printf("'%s'\n", symbol_name);

                if( !(pCur_item = malloc(sizeof(symbol_relation_t))) )
                {
                    err("malloc '%d' fail \n", sizeof(symbol_relation_t));
                    break;
                }
                memset(pCur_item, 0x0, sizeof(symbol_relation_t));

                pCur_item->crc_id = calc_crc32((uint8_t*)symbol_name, strlen(symbol_name));
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
                continue;
            }

            pAct_str = strstr(pAct_str, "(call ");
            if( !pAct_str )     continue;

            rval = sscanf(pAct_str, "(call %*[^\"]\"%[^\"]", symbol_name);
            if( rval == 1 )
            {
                uint32_t        crc_id = 0;
                symbol_itm_t    *pNew_callee_symbol = 0;

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

                printf("  call-> '%s'\n", symbol_name);

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
    } while(0);

    return rval;
}

static int
_dump_call_graph_table(
    call_graph_table_t  *pCall_graph_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    char        *pOut_name = "call_graph_list.txt";

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
                fprintf(fout, "'%s' -> '%s' x%08x\n", pCur->symbol_name, pCur_callee->symbol_name, pCur_callee->crc_id);
                pCur_callee = pCur_callee->next;
            }

            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}
//===================================================
int main(int argc, char **argv)
{
    int                 rval = 0;
    dictionary          *pIni = 0;
    const char          *pPath = 0;
    partial_read_t      hReader_func_addr = {0};
    partial_read_t      hReader_symbol_db = {0};
    partial_read_t      hReader_expand = {0};
    symbol_table_t      symbol_table = {0};
    call_graph_table_t  call_graph_table = {0};

    do {
        pIni = iniparser_load(argv[1]);
        if( pIni == NULL )
        {
            err("cannot parse file: '%s'\n", argv[1]);
            rval = -1;
            break;
        }

        // iniparser_dump(pIni, stderr);

        //--------------------------------
        // generate basic function flow
        pPath = iniparser_getstring(pIni, "func_addr:bin_file_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no bin_file_path\n");
            break;
        }

        hReader_func_addr.alignment     = iniparser_getint(pIni, "func_addr:addr_alignment", 0);
        hReader_func_addr.is_big_endian = iniparser_getboolean(pIni, "func_addr:is_big_endian", 0);
        if( (rval = _create_reader(&hReader_func_addr, pPath)) )
            break;

        pPath = iniparser_getstring(pIni, "func_addr:symbol_table_path", NULL);
        if( !pPath )
        {
            rval = -1;
            err("%s", "no symbol_table_path\n");
            break;
        }

        hReader_symbol_db.alignment     = 0;
        hReader_symbol_db.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_symbol_db, pPath)) )
            break;

        _create_symbol_table(&hReader_symbol_db, &symbol_table);

        _addr_to_func(&hReader_func_addr, &symbol_table);

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
        _dump_call_graph_table(&call_graph_table);
    }while(0);

    _destroy_symbol_table(&symbol_table);
    _destory_call_graph_table(&call_graph_table);
    _destroy_reader(&hReader_func_addr);
    _destroy_reader(&hReader_symbol_db);
    _destroy_reader(&hReader_expand);

    if( pIni )      iniparser_freedict(pIni);

    mlead_dump();
    return rval;
}
