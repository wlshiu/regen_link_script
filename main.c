
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "iniparser.h"
#include "partial_read.h"
#include "symbol_info.h"
#include "crc32.h"

#include "mleak_check.h"
//===================================================
#define MAX_BUFFER_SIZE                     (2 << 20)
#define FILE_NAME__BASIC_FUNC_FLOW          "basic_func_flow.txt"

//===================================================
#define err(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)


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
            char            symbol_name[MAX_SYMBOL_NAME_LENGTH] = {0};

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
            fprintf(fout, "* (.text.%s)\n", pCur->symbol_name);
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

            pAct_str = (char*)pHReader_expand->pCur;

            pHReader_expand->pCur += (strlen((char*)pHReader_expand->pCur) + 1);

            rval = sscanf(pAct_str, ";; Function %s (%*s)", symbol_name);
            if( rval == 1 )
            {
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

        memset(pCall_graph_table, 0x0, sizeof(call_graph_table_t));
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
                fprintf(fout, "'%s' -> '%s'\n", pCur->symbol_name, pCur_callee->symbol_name);
                pCur_callee = pCur_callee->next;
            }

            pCur = pCur->next;
        }
    } while(0);

    if( fout )      fclose(fout);

    return rval;
}

static int
_create_lib_obj_table(
    partial_read_t      *pHReader_map,
    lib_table_t         *pLib_table)
{
    int                 rval = 0;

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
            char                ext_lib[4] = {0};
            char                ext_obj[4] = {0};
            char                lib_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            char                obj_name[MAX_SYMBOL_NAME_LENGTH] = {0};
            lib_itm_t           *pAct_lib = 0;
            obj_itm_t           *pAct_obj = 0;

            pAct_str = (char*)pHReader_map->pCur;

            pHReader_map->pCur += (strlen((char*)pHReader_map->pCur) + 1);

            for(i = 0; i < strlen(pAct_str); ++i)
                pAct_str[i] = (pAct_str[i] == '\\') ? '/' : pAct_str[i];

            // look up lib_name exist or not in a line
            pAct_str = strrchr(pAct_str, '/');
            if( !pAct_str )     continue;

            rval = sscanf(pAct_str, "/%[^.].%[^(](%[^.].%[^)]) ", lib_name, ext_lib, obj_name, ext_obj);
            if( rval != 4 )     continue;

            if( strncmp(ext_lib, "a", 2) )
            {
                err("wrong lib name '%s.%s'\n", lib_name, ext_lib);
                continue;
            }

            if( strncmp(ext_obj, "o", 2) )
            {
                err("wrong obj name '%s.%s'\n", obj_name, ext_obj);
                continue;
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
    lib_table_t         *pLib_table)
{
    int         rval = 0;
    FILE        *fout = 0;
    char        *pOut_name = "lib_obj_list.txt";

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
            fprintf(fout, "\n/* %s*/\n", pCur->lib_name);
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
    symbol_table_t      *pSymbol_table_lite)
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

        if( !pAct )     break;

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

                _look_up_relation(pCall_graph_table, pSymbol_callee->crc_id, pSymbol_callee->symbol_name, pSymbol_table_lite);
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
    symbol_table_t      *pSymbol_table_lite)
{
    int         rval = 0;

#if 0
    if( !(g_fFunc_tree = fopen("func_tree.txt", "wb")) )
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

            pAct_str = (char*)pHReader_basic_func_flow->pCur;

            pHReader_basic_func_flow->pCur += (strlen((char*)pHReader_basic_func_flow->pCur) + 1);

            rval = sscanf(pAct_str, "* (.text.%[^)])", symbol_name);
            if( rval != 1 )
            {
                continue;
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
            _look_up_relation(pCall_graph_table, crc_id, symbol_name, pSymbol_table_lite);
        }
    }

    if( g_fFunc_tree )      fclose(g_fFunc_tree);

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
    partial_read_t      hReader_map = {0};
    partial_read_t      hReader_basic_func_flow = {0};
    symbol_table_t      symbol_table_all = {0};
    symbol_table_t      symbol_table_lite = {0};
    call_graph_table_t  call_graph_table = {0};
    lib_table_t         lib_table = {0};

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

        _create_symbol_table(&hReader_symbol_db, &symbol_table_all);
        _destroy_reader(&hReader_symbol_db);

        // binary address file
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

        _addr_to_func(&hReader_func_addr, &symbol_table_all);
        _destroy_reader(&hReader_func_addr);

        _destroy_symbol_table(&symbol_table_all);

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

        _dump_call_graph_table(&call_graph_table);

        //-------------------------------
        // complete functions base on basic_func_flow
        pPath = FILE_NAME__BASIC_FUNC_FLOW;
        hReader_basic_func_flow.alignment     = 0;
        hReader_basic_func_flow.is_big_endian = 0;
        if( (rval = _create_reader(&hReader_basic_func_flow, pPath)) )
            break;

        _complete_func_table(&hReader_basic_func_flow, &call_graph_table, &symbol_table_lite);
        _destroy_reader(&hReader_basic_func_flow);

        _dump_symbol_table("final_symbol_list.txt", &symbol_table_lite);

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
        _destroy_reader(&hReader_map);

        _dump_lib_obj_table(&lib_table);
    }while(0);

    _destroy_symbol_table(&symbol_table_all);
    _destroy_symbol_table(&symbol_table_lite);
    _destory_call_graph_table(&call_graph_table);
    _destory_lib_obj_table(&lib_table);
    _destroy_reader(&hReader_func_addr);
    _destroy_reader(&hReader_symbol_db);
    _destroy_reader(&hReader_expand);
    _destroy_reader(&hReader_map);
    _destroy_reader(&hReader_basic_func_flow);

    if( pIni )      iniparser_freedict(pIni);

    mlead_dump();
    return rval;
}
