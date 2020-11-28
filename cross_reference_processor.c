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

uint32_t data[1024*1024*4];

struct verse
{
};

int main( int argc, char ** argv )
{
	if( argc != 2 )
	{
		fprintf( stderr, "Error: usage: %s [cross_references.txt]\n", argv[0] );
		return -6;
	}
	stbi_write_png_compression_level = 9;
	FILE * f = fopen( argv[1], "rb" );
	if( !f )
	{
		fprintf( stderr, "Error: could not open %s\n", argv[1] );
		return -7;
	}
	char * line;
	while( line = OSGLineFromFile( f ) )
	{
		free( line );
	}
	fclose( f );

	mkdir( "assets", 0777 );
	int r = stbi_write_png( "assets/truncated_cross_references.png", 1024, 1024, 4, data, 4*1024 );
	printf( "stbi_write_png(...) = %d\n", r );
	if( 0 == r )
	{
		fprintf( stderr, "Error writing PNG file.\n" );
		return -5;
	}
	return 0;
}

