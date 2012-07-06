/*
 * Copyright 1993-2002 Christopher Seiwald and Perforce Software, Inc.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

/*  This file is ALSO:
 *  Copyright 2001-2004 David Abrahams.
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or http://www.boost.org/LICENSE_1_0.txt)
 */

/*
 * filesys.h - OS specific file routines
 */

#ifndef FILESYS_DWA20011025_H
#define FILESYS_DWA20011025_H

#include "hash.h"
#include "lists.h"
#include "object.h"
#include "pathsys.h"


typedef void (*scanback)( void *closure, OBJECT * file, int found, time_t t );

void file_dirscan( OBJECT * dir, scanback func, void * closure );
void file_archscan( char const * arch, scanback func, void * closure );

int file_time( OBJECT * filename, time_t * time );

void file_build1( PATHNAME * f, string * file ) ;
int file_is_file( OBJECT * filename );
int file_mkdir( char const * pathname );

typedef struct file_info_t
{
    OBJECT * name;
    short is_file;
    short is_dir;
    unsigned long size;
    time_t time;
    LIST * files;
} file_info_t;


/* Returns a pointer to information about file 'filename', creating it as
 * necessary. If created, the structure will be default initialized.
 */
file_info_t * file_info( OBJECT * filename );

/* Returns information about a file, queries the OS if needed. Will return 0 if
 * the file does not exist.
 */
file_info_t * file_query( OBJECT * filename );

void file_done();

/* Marks a path/file to be removed when JAM exits. */
void file_remove_atexit( OBJECT * const path );

#endif
