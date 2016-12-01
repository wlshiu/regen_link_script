#include <stdio.h>
#include <stdlib.h>

#include "iniparser.h"
#include "partial_read.h"

//===================================================
#define MAX_BUFFER_SIZE         (2 << 20)
//===================================================
#define err(str, args...)           fprintf(stderr, "%s[%d] " str, __func__, __LINE__, ## args)


//===================================================
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
            err("malloc '%d' fail \n", pHReader->buf_size);
            rval = -1;
            break;
        }

        pHReader->remain_length = pHReader->buf_size;
        pHReader->pEnd          = pHReader->pBuf + pHReader->buf_size;

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

//===================================================
int main(int argc, char **argv)
{
    int             rval = 0;
    dictionary      *pIni = 0;
    const char      *pPath = 0;
    partial_read_t  hReader_func_addr = {0};
    partial_read_t  hReader_symbol_db = {0};

    do {
        pIni = iniparser_load(argv[1]);
        if( pIni == NULL )
        {
            err("cannot parse file: '%s'\n", argv[1]);
            rval = -1;
            break;
        }

        // iniparser_dump(pIni, stderr);

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

    }while(0);

    _destroy_reader(&hReader_func_addr);
    _destroy_reader(&hReader_symbol_db);

    if( pIni )      iniparser_freedict(pIni);

    return rval;
}
