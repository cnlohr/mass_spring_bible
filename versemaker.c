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

#define NO_APOCRYPHA
#define MINIMUM_CROSSREFERENCE 5

int ascii_histogram[256];

typedef char * str;
typedef int intnomat;

CNRBTREETEMPLATE( str, int, RBstrcmp, RBstrcpy, RBstrdel );
CNRBTREETEMPLATE( intnomat, str, RBptrcmpnomatch, RBptrcpy, RBnullop ); //nomatch = treat as a multimap it is [-count] => word
CNRBTREETEMPLATE( int, str, RBptrcmp, RBptrcpy, RBnullop );
CNRBTREETEMPLATE( int, int, RBptrcmp, RBptrcpy, RBnullop );

cnrbtree_strint * wordcounts;
cnrbtree_intnomatstr * countwords;
cnrbtree_intstr * versedictionary; //<book<<16 | chapter<<8 | verse> => string of verse.
cnrbtree_intint * verseatlas;   //Points to the position in the texture of the text of the verse.
cnrbtree_intint * verseindexatlas; //[verse key->location in verse index] Points to verse
cnrbtree_intint * chapteratlas;    //[chapter number->location in chapter index] Points to verseindexatlas

//Pointers are stored as colors <RG,B> where x = R+G*256,B.  They are not divisible by colors.

int versecount;
int maxbook;
int numchapters;
uint32_t verserootposition;
uint32_t chapterrootposition;
uint32_t bookrootposition;

//Texture is stored in groups of six bytes, even and odd rows are paired together for cache coherency. 
#define TEXTURE_W 4096
#define TEXTURE_H 512
#define MLW 2  //For indexing purposes, minimum line allocation unit.
uint8_t TextureData[TEXTURE_H*TEXTURE_W*3];

//Allocate a group of 2 pixels (8 values)
uint32_t AllocateTextureSpace( int doublepixelcount, int is_root_entry )
{
	static int AllocatedRows[TEXTURE_H/MLW];

	//0,0 is a special value that ought not be allocated.
	if( AllocatedRows[0] == 0 )
	{
		AllocatedRows[0] = 1;
	}
	int i;
	for( i = 0; i < TEXTURE_H/MLW; i++ )
	{
		if( AllocatedRows[i] + doublepixelcount <= TEXTURE_W )
		{
			//If root entry, must be first entry on line.
			if( is_root_entry && AllocatedRows[i] ) continue;

			uint32_t ret = ((i)<<16) | AllocatedRows[i];
			AllocatedRows[i] += doublepixelcount;
			return ret;
		}
	}
	fprintf( stderr, "Error: Could not allocate block of size %d\n", doublepixelcount );
	exit( -95 );
}

char * TextureIndex( int alloc, int advance )
{
	int ay = (alloc>>16)*2;
	int ax = alloc&0xffff;
	return &TextureData[ ( (ay+(advance%6)/3)*TEXTURE_W+ax+(advance/6))*3+(advance%3)];
}


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


int LoadBibleAndGenerateTSV( const char * biblezip, const char * tsvfile )
{
	biblefile = zip_open( biblezip, 0, 'r' );

	if( !biblefile )
	{
		fprintf( stderr, "Error: Could not open zip file %s\n", biblezip );
		return -7;
	}

	FILE * outfile = fopen( tsvfile, "wb" );
	if( !outfile )
	{
		fprintf( stderr, "Error: failed to open outfile %s\n", tsvfile );
	}

	int i, n = zip_total_entries( biblefile );
	for (i = 0; i < n; ++i)
	{
		zip_entry_openbyindex( biblefile, i );
		{
			const char *name = zip_entry_name( biblefile );
			int isdir = zip_entry_isdir( biblefile );
			unsigned long long size = zip_entry_size( biblefile );
			if( isdir || strncmp( "eng-web", name, 7 ) ) goto skip_chapter;
			if( strlen( name ) < 12+6 ) goto skip_chapter;
			int booknumber = GetBookNumberFromWEBCode( name + 12 );
			int chapternumber = atoi( name + 16 );

			if( booknumber < 1 ) goto skip_chapter;

#ifdef NO_APOCRYPHA
			if( booknumber > 66 ) goto skip_chapter;
#endif
			if( booknumber > maxbook ) maxbook = booknumber;
		    char * buf = calloc(sizeof(unsigned char), size+1);
		    zip_entry_noallocread(biblefile, (void *)buf, size);
			buf[size] = 0;

			numchapters++;
			printf( "%s\n", name );

			char * st = strtok( buf, "\n" );
			int verse = 0;
			while( st )
			{
				st = strtok( 0, "\n" );
				if( !st ) break;
				int linelen = strlen( st );
				for( ; linelen > 0; linelen-- )
					if( st[linelen-1] != 32 ) break;
				st[linelen] = 0;

				//Now we're here, we have the text of the verse 'st'.
				int pli, plo;
				for( pli = 0, plo = 0; pli < linelen; pli++, plo++ )
				{
					char c = st[pli];
					if( ((unsigned char)c) >= 0x80 )
					{
						pli+=2;
						if( pli>= linelen ) { goto finish_out_chapter; }
						switch( (unsigned char)st[pli] )
						{
						case 0x9c: //open quote, but we just do double quote.
							st[plo] = '\"';
							break;
						case 0x9d: //closed quote, but we just do double quote.
							st[plo] = '\"';
							break;
						case 0x99: //apostophe, but we just do double quote.
							st[plo] = '\'';
							break;
						case 0x98: //open single quote but we just do double quote.
							st[plo] = '\'';
							break;
						case 0x94: //closed single quote but we just do double quote.
							st[plo] = '\'';
							break;
						default:
							st[plo+1] = '#';
							fprintf( stderr, "Error: unknown unicode value found in [%s] [%d] [%d] %s: %s: %02x\n", name, verse, linelen, buf, st, st[pli] );
							return -9;
							break;
						}
					}
					else
					{
						st[plo] = c;
					}
					ascii_histogram[st[plo]]++;
				}
				st[plo] = 0;
				if( verse >= 1 )
				{
					uint32_t dictid = (booknumber<<16) | (chapternumber<<8) | (verse);
					if( verse > 255 || chapternumber > 255 || booknumber > 255 )
					{
						fprintf( stderr, "Error: verse index invalid. [%d %d %d]\n", booknumber, chapternumber, verse );
						return -9;
					}
					RBA( versedictionary, dictid ) = strdup( st );
					fprintf( outfile, "%d\t%d\t%d\t%s\n", booknumber, chapternumber, verse, st );
					//Word count analysis.
					char * thiswordstart = st;
					for( pli = 0; pli < plo; pli++ )
					{
						char c = st[pli];
						if( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) )
						{
							//continue
						}
						else
						{
							st[pli] = 0;
							int wlen = strlen( thiswordstart );
							if( wlen > 1 )
							{
								if( RBHAS( wordcounts, thiswordstart ) )
									RBA( wordcounts, thiswordstart )++;
								else
									RBA( wordcounts, thiswordstart ) = 1;
							}
							thiswordstart = st + pli + 1;
						}
					}
				}

				verse++;
			}
finish_out_chapter:
			free( buf );
		}
skip_chapter:
		zip_entry_close( biblefile );
	}
	zip_close( biblefile );

	//Invert the map.
	RBFOREACH( strint, wordcounts, i )
	{
		RBA( countwords, -i->data ) = i->key;
	}
	n = 0;
	for( i = 0; i < 256; i++ )
	{
		if( ascii_histogram[i] )
		{
			n++;
			printf( "%d: %d %c : %d\n", n, i, i, ascii_histogram[i] ); 
		}
	}
	printf( "Unique Chars: %d\n", n );
	//printf( "Top words: \n" );
	//RBFOREACH( intnomatstr, countwords, i )
	//{
	//	printf( "%s: %d\n", i->data, -i->key );
	//	n++;
	//	if( n == 255 ) break;
	//}

	uint32_t allocplace;

	//Iterate over all verses and write the tex of the verses into the table.
	RBFOREACH( intstr, versedictionary, i )
	{
		uint32_t versekey = i->key;
		const char * text = i->data;
		int len = strlen( text );
		allocplace = AllocateTextureSpace( (len + 6)/6, 0 );
		RBA( verseatlas, versekey ) = allocplace;
		int i;
		for( i = 0; i < len; i++ )
		{
			*TextureIndex(allocplace,i) = text[i];
		}
		versecount++;
	}

	{
		uint32_t blankgroup = AllocateTextureSpace( TEXTURE_W, 1 );
		int i;
		for( i = 0; i < TEXTURE_W*2; i++ )
		{
			*TextureIndex( blankgroup, 0+i*3 ) = 0xff;
			*TextureIndex( blankgroup, 1+i*3 ) = 0xff;
			*TextureIndex( blankgroup, 2+i*3 ) = 0x00;
		}
	}

	verserootposition = AllocateTextureSpace( TEXTURE_W, 1 );
	{
		int vno_this_line = 0;
		int versepos = verserootposition;
		int verseno = 0;
		RBFOREACH( intstr, versedictionary, i )
		{
			if( vno_this_line == TEXTURE_W )
			{
				vno_this_line = 0;
				versepos = AllocateTextureSpace( TEXTURE_W, 1 );
			}

			uint32_t versekey =i->key;
			uint32_t location_of_verse = RBA( verseatlas, versekey );
			*TextureIndex( versepos, 0+vno_this_line*6 ) = location_of_verse>>0;
			*TextureIndex( versepos, 1+vno_this_line*6 ) = location_of_verse>>8;
			*TextureIndex( versepos, 2+vno_this_line*6 ) = location_of_verse>>16;
			//printf( "%d -> %08x -> %08x %08x\n", verseno, location_of_verse, versepos, vno_this_line );
			RBA( verseindexatlas, versekey ) = versepos + vno_this_line;

			verseno++;
			vno_this_line++;
		}
	}

	{
		uint32_t blankgroup = AllocateTextureSpace( TEXTURE_W, 1 );
		int i;
		for( i = 0; i < TEXTURE_W*2; i++ )
		{
			*TextureIndex( blankgroup, 0+i*3 ) = 0x00;
			*TextureIndex( blankgroup, 1+i*3 ) = 0xff;
			*TextureIndex( blankgroup, 2+i*3 ) = 0xff;
		}
	}

	chapterrootposition = AllocateTextureSpace( (numchapters + 2)/2, 1 );
	bookrootposition = AllocateTextureSpace( (maxbook + 2)/2, 1 );
	{
		int chapterno = 0; //Global chapter number
		int bookno = 0;
		RBFOREACH( intstr, versedictionary, i )
		{
			uint32_t versekey =i->key;
			uint32_t book    =  versekey >> 16;
			uint32_t chapter = (versekey >> 8)&0xff;
			uint32_t verse   = (versekey >> 0)&0xff;
			if( verse != 1 ) continue;
			//Verse is 1 - this is the first verse in this chapter.

			uint32_t verselocation = RBA( verseindexatlas, versekey );
			*TextureIndex( chapterrootposition, chapterno*6+0 ) = verselocation>>0;
			*TextureIndex( chapterrootposition, chapterno*6+1 ) = verselocation>>8;
			*TextureIndex( chapterrootposition, chapterno*6+2 ) = verselocation>>16;

			uint32_t chapterloc = chapterrootposition + chapterno;
			RBA( chapteratlas, chapterno ) = chapterloc;

//			printf( "CHAPTER [%d] [%d,%d]\n", verselocation, (verselocation)%65536, (verselocation)/65536 );

			chapterno++;

			if( chapter != 1 ) continue;

			//printf( "BOOK [%20s] [%d] [%d,%d]\n", BibleBooksCorrelation[bookno*4+4], chapterloc, (chapterloc)%65536, (chapterloc)/65536 );

			*TextureIndex( bookrootposition, bookno*6+0 ) = chapterloc>>0;
			*TextureIndex( bookrootposition, bookno*6+1 ) = chapterloc>>8;
			*TextureIndex( bookrootposition, bookno*6+2 ) = chapterloc>>16;

			bookno++;
		}
	}


	{
		uint32_t blankgroup = AllocateTextureSpace( TEXTURE_W, 1 );
		int i;
		for( i = 0; i < TEXTURE_W*2; i++ )
		{
			*TextureIndex( blankgroup, 0+i*3 ) = 0xff;
			*TextureIndex( blankgroup, 1+i*3 ) = 0x00;
			*TextureIndex( blankgroup, 2+i*3 ) = 0xff;
		}
	}

	printf( "Books: %d Chapters: %d Verses: %d\n", maxbook, numchapters, versecount );

	fclose( outfile );
	return 0;
}

//Returns 0 on error.
uint32_t GetVerseNumberFromOpenBible( const char * v )
{
	//Convert 'Rev.3.5' to {book}{chapter}{verse} format.
	int rl = 0;
	int vlen = strlen( v );
	int chaplen = 0;
	for( rl = 0; rl < vlen; rl++ )
	{
		if( v[rl] == '.' ) break;
	}
	if( rl == vlen ) goto fail;
	int booklen = rl;
	int book;
	for( book = 0; BibleBooksCorrelation[book*4+1]; book++ )
	{
		if( strncmp( BibleBooksCorrelation[book*4+1], v, booklen ) == 0 )
		{
			break;
		}
	}
	if( book > 66 ) goto fail;
	int chapter = atoi( v+rl+1 );
	for( rl++; rl < vlen; rl++ )
	{
		if( v[rl] == '.' ) break;
	}
	int verse = atoi( v+rl+1 );
	if( chapter <= 0 || verse <= 0 ) goto fail;
	return (book<<16) | (chapter<<8) | (verse);
fail:
	fprintf( stderr, "Failed to read verseid for %s\n", v );
	return 0;
}

int LoadBibleCrossReferences( const char * crossreferencefile )
{
	FILE * f = fopen( crossreferencefile, "r" );
	if( !f )
	{
		fprintf( stderr, "Error: Could not open %s\n", crossreferencefile );
		return -6;
	}

	int refno = 0;
	char * line;
	int refs = 0;
	while( ( line = OSGLineFromFile( f ) ) )
	{
		if( refno == 0 ) goto skip;
		char * markA = strtok( line, "\t" );
		char * markB = strtok( 0, "\t" );
		char * markC = strtok( 0, "\t" );
		if( !markA || !markB || !markC ) goto skip;
		int strength = atoi( markC );
		if( strength < MINIMUM_CROSSREFERENCE) goto skip;
		int v1 = GetVerseNumberFromOpenBible( markA );
		int v2 = GetVerseNumberFromOpenBible( markB );
		if( !v1 || !v2 ) return -9;
		refs++;
		printf( "/%s[%d]/%s[%d]/%s/\n", markA, v1, markB, v2, markC );
skip:
		free( line );
		refno++;
	}
	printf( "Got %d references\n", refs );
	fclose( f );
	return 0;
}

int main( int argc, char ** argv )
{
	wordcounts = cnrbtree_strint_create();
	countwords = cnrbtree_intnomatstr_create();
	versedictionary = cnrbtree_intstr_create(); //Index is BOOKno<<16, CHAPTno<<8, VERSEno
	verseatlas = cnrbtree_intint_create(); // Points to text
	verseindexatlas = cnrbtree_intint_create(); // Points to verse index
	chapteratlas = cnrbtree_intint_create(); // Points to verses

	stbi_write_png_compression_level = 9;
	if( argc != 4 )
	{
		fprintf( stderr, "Error: usage: %s [eng-web_readaloud.zip] [outfile.tsv] [cross-reference-list.txt]\n", argv[0] );
		return -6;
	}

	if( LoadBibleAndGenerateTSV( argv[1], argv[2] ) )
	{
		return -99;
	}

	if( LoadBibleCrossReferences( argv[3] ) )
	{
		return -100;
	}


	mkdir( "assets", 0777 );
	int r = stbi_write_png( "assets/bible_data.png", TEXTURE_W, TEXTURE_H, 3, TextureData, 3*TEXTURE_W );
	printf( "stbi_write_png(...) = %d\n", r );
	if( 0 == r )
	{
		fprintf( stderr, "Error writing PNG file.\n" );
		return -5;
	}

	return 0;
}

