#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "thirdparty/stb_image_write.h"

#define OSG_STATIC
#include "thirdparty/osg_aux.h"

#define CNRBTREE_IMPLEMENTATION
#include "thirdparty/cnrbtree.h"

#include "thirdparty/zip.c"

#include "chaptermap.h"

struct zip_t * biblefile;




int GetBookNumberFromWEBCode( const char * WEBCode )
{
	int i;
	for( i = 0; BibleBooksCorrelation[i*4]; i++ )
	{
		if( strncmp( BibleBooksCorrelation[i*4+2], WEBCode, 3 ) == 0 )
		{
			return i;
		}
	}
	return 0;
};

int main( int argc, char ** argv )
{
	stbi_write_png_compression_level = 9;
	if( argc != 2 )
	{
		fprintf( stderr, "Error: usage: %s [eng-web_readaloud.zip]\n", argv[0] );
		return -6;
	}

	biblefile = zip_open( argv[1], 0, 'r' );

	if( !biblefile )
	{
		fprintf( stderr, "Error: Could not open zip file %s\n", argv[1] );
		return -7;
	}

	int i, n = zip_total_entries( biblefile );
	for (i = 0; i < n; ++i)
	{
		zip_entry_openbyindex( biblefile, i );
		{
			const char *name = zip_entry_name( biblefile );
			int isdir = zip_entry_isdir( biblefile );
			unsigned long long size = zip_entry_size( biblefile );
			if( isdir || strncmp( "eng-web", name, 7 ) ) continue;
			if( strlen( name ) < 12+6 ) continue;
			int booknumber = GetBookNumberFromWEBCode( name + 12 );
			int chapternumber = atoi( name + 16 );

			if( booknumber < 1 ) continue;

		    char * buf = calloc(sizeof(unsigned char), size);
		    zip_entry_noallocread(biblefile, (void *)buf, size);

			printf( "%s:%d %d\n", name, booknumber, chapternumber );

			free( buf );
		}
		zip_entry_close( biblefile );
	}
	zip_close( biblefile );
	return 0;
}

