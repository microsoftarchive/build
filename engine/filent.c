/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

/*  This file is ALSO:
 *  Copyright 2001-2004 David Abrahams.
 *  Copyright 2005 Rene Rivera.
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or http://www.boost.org/LICENSE_1_0.txt)
 */

/*
 * filent.c - scan directories and archives on NT
 *
 * External routines:
 *  file_archscan() - scan an archive for files
 *  file_mkdir()    - create a directory
 *
 * External routines called only via routines in filesys.c:
 *  file_collect_dir_content_() - collects directory content information
 *  file_dirscan_()             - OS specific file_dirscan() implementation
 *  file_query_()               - query information about a path from the OS
 */

#include "jam.h"
#ifdef OS_NT
#include "filesys.h"

#include "object.h"
#include "pathsys.h"
#include "strings.h"

#ifdef __BORLANDC__
# undef FILENAME  /* cpp namespace collision */
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <assert.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>


/*
 * file_collect_dir_content_() - collects directory content information
 */

int file_collect_dir_content_( file_info_t * const d )
{
    PATHNAME f;
    string pathspec[ 1 ];
    string pathname[ 1 ];
    LIST * files = L0;
    int d_length;

    assert( d );
    assert( d->is_dir );
    assert( list_empty( d->files ) );

    d_length = strlen( object_str( d->name ) );

    memset( (char *)&f, '\0', sizeof( f ) );
    f.f_dir.ptr = object_str( d->name );
    f.f_dir.len = d_length;

    /* Prepare file search specification for the findfirst() API. */
    if ( !d_length )
        string_copy( pathspec, ".\\*" );
    else
    {
        /* We can not simply assume the given folder name will never include its
         * trailing path separator or otherwise we would not support the Windows
         * root folder specified without its drive letter, i.e. '\'.
         */
        char const trailingChar = object_str( d->name )[ d_length - 1 ] ;
        string_copy( pathspec, object_str( d->name ) );
        if ( ( trailingChar != '\\' ) && ( trailingChar != '/' ) )
            string_append( pathspec, "\\" );
        string_append( pathspec, "*" );
    }

    /* The following code for collecting information about all files in a folder
     * needs to be synchronized with how the file_query() operation is
     * implemented (collects information about a single file).
     */
    {
        /* FIXME: Avoid duplicate FindXXX Windows API calls here and in the code
         * determining a normalized path.
         */
        WIN32_FIND_DATA finfo;
        HANDLE const findHandle = FindFirstFileA( pathspec->value, &finfo );
        if ( findHandle == INVALID_HANDLE_VALUE )
        {
            string_free( pathspec );
            return -1;
        }

        string_new( pathname );
        do
        {
            OBJECT * pathname_obj;

            f.f_base.ptr = finfo.cFileName;
            f.f_base.len = strlen( finfo.cFileName );
            string_truncate( pathname, 0 );
            path_build( &f, pathname );

            pathname_obj = object_new( pathname->value );
            path_key__register_long_path( pathname_obj );
            files = list_push_back( files, pathname_obj );
            {
                file_info_t * const ff = file_info( pathname_obj );
                ff->is_dir = finfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
                ff->is_file = !ff->is_dir;
                timestamp_from_filetime( &ff->time, &finfo.ftLastWriteTime );
            }
        }
        while ( FindNextFile( findHandle, &finfo ) );

        FindClose( findHandle );
    }

    string_free( pathname );
    string_free( pathspec );

    d->files = files;
    return 0;
}


/*
 * file_dirscan_() - OS specific file_dirscan() implementation
 */

void file_dirscan_( file_info_t * const d, scanback func, void * closure )
{
    assert( d );
    assert( d->is_dir );

    /* Special case \ or d:\ : enter it */
    {
        char const * const name = object_str( d->name );
        if ( name[ 0 ] == '\\' && !name[ 1 ] )
        {
            (*func)( closure, d->name, 1 /* stat()'ed */, &d->time );
        }
        else if ( name[ 0 ] && name[ 1 ] == ':' && name[ 2 ] && !name[ 3 ] )
        {
            /* We have just entered a 3-letter drive name spelling (with a
             * trailing slash), into the hash table. Now enter its two-letter
             * variant, without the trailing slash, so that if we try to check
             * whether "c:" exists, we hit it.
             *
             * Jam core has workarounds for that. Given:
             *    x = c:\whatever\foo ;
             *    p = $(x:D) ;
             *    p2 = $(p:D) ;
             * There will be no trailing slash in $(p), but there will be one in
             * $(p2). But, that seems rather fragile.
             */
            OBJECT * const dir_no_slash = object_new_range( name, 2 );
            (*func)( closure, d->name, 1 /* stat()'ed */, &d->time );
            (*func)( closure, dir_no_slash, 1 /* stat()'ed */, &d->time );
            object_free( dir_no_slash );
        }
    }
}


/*
 * file_mkdir() - create a directory
 */

int file_mkdir( char const * const path )
{
    return _mkdir( path );
}


/*
 * file_query_() - query information about a path from the OS
 *
 * The following code for collecting information about a single file needs to be
 * synchronized with how the file_collect_dir_content_() operation is
 * implemented (collects information about all files in a folder).
 */

int file_query_( file_info_t * const info )
{
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    char const * const pathstr = object_str( info->name );
    char const * const pathspec = *pathstr ? pathstr : ".";

    if ( !GetFileAttributesExA( pathspec, GetFileExInfoStandard, &fileData ) )
        return -1;

    info->is_dir = fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    info->is_file = !info->is_dir;
    timestamp_from_filetime( &info->time, &fileData.ftLastWriteTime );
    return 0;
}


/*
 * file_archscan() - scan an archive for files
 */

/* Straight from SunOS */

#define ARMAG  "!<arch>\n"
#define SARMAG  8

#define ARFMAG  "`\n"

struct ar_hdr {
    char ar_name[ 16 ];
    char ar_date[ 12 ];
    char ar_uid[ 6 ];
    char ar_gid[ 6 ];
    char ar_mode[ 8 ];
    char ar_size[ 10 ];
    char ar_fmag[ 2 ];
};

#define SARFMAG  2
#define SARHDR  sizeof( struct ar_hdr )

void file_archscan( char const * archive, scanback func, void * closure )
{
    struct ar_hdr ar_hdr;
    char * string_table = 0;
    char buf[ MAXJPATH ];
    long offset;
    int const fd = open( archive, O_RDONLY | O_BINARY, 0 );

    if ( fd < 0 )
        return;

    if ( read( fd, buf, SARMAG ) != SARMAG || strncmp( ARMAG, buf, SARMAG ) )
    {
        close( fd );
        return;
    }

    offset = SARMAG;

    if ( DEBUG_BINDSCAN )
        printf( "scan archive %s\n", archive );

    while ( ( read( fd, &ar_hdr, SARHDR ) == SARHDR ) &&
        !memcmp( ar_hdr.ar_fmag, ARFMAG, SARFMAG ) )
    {
        long lar_date;
        long lar_size;
        char * name = 0;
        char * endname;

        sscanf( ar_hdr.ar_date, "%ld", &lar_date );
        sscanf( ar_hdr.ar_size, "%ld", &lar_size );

        lar_size = ( lar_size + 1 ) & ~1;

        if ( ar_hdr.ar_name[ 0 ] == '/' && ar_hdr.ar_name[ 1 ] == '/' )
        {
            /* This is the "string table" entry of the symbol table, holding
             * filename strings longer than 15 characters, i.e. those that do
             * not fit into ar_name.
             */
            string_table = BJAM_MALLOC_ATOMIC( lar_size + 1 );
            if ( read( fd, string_table, lar_size ) != lar_size )
                printf( "error reading string table\n" );
            string_table[ lar_size ] = '\0';
            offset += SARHDR + lar_size;
            continue;
        }
        else if ( ar_hdr.ar_name[ 0 ] == '/' && ar_hdr.ar_name[ 1 ] != ' ' )
        {
            /* Long filenames are recognized by "/nnnn" where nnnn is the
             * string's offset in the string table represented in ASCII
             * decimals.
             */
            name = string_table + atoi( ar_hdr.ar_name + 1 );
            for ( endname = name; *endname && *endname != '\n'; ++endname );
        }
        else
        {
            /* normal name */
            name = ar_hdr.ar_name;
            endname = name + sizeof( ar_hdr.ar_name );
        }

        /* strip trailing white-space, slashes, and backslashes */

        while ( endname-- > name )
            if ( !isspace( *endname ) && ( *endname != '\\' ) && ( *endname !=
                '/' ) )
                break;
        *++endname = 0;

        /* strip leading directory names, an NT specialty */
        {
            char * c;
            if ( c = strrchr( name, '/' ) )
                name = c + 1;
            if ( c = strrchr( name, '\\' ) )
                name = c + 1;
        }

        sprintf( buf, "%s(%.*s)", archive, endname - name, name );
        {
            OBJECT * const member = object_new( buf );
            timestamp time;
            timestamp_init( &time, (time_t)lar_date, 0 );
            (*func)( closure, member, 1 /* time valid */, &time );
            object_free( member );
        }

        offset += SARHDR + lar_size;
        lseek( fd, offset, 0 );
    }

    close( fd );
}

#endif  /* OS_NT */
