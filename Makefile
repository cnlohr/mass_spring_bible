all : everything versemaker

resync_third_party :
	mkdir -p thirdparty
	wget https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -O thirdparty/stb_image_write.h
	wget https://raw.githubusercontent.com/cnlohr/cntools/master/osg_aux.h -O thirdparty/osg_aux.h
	wget https://raw.githubusercontent.com/cnlohr/cntools/master/cnrbtree/cnrbtree.h -O thirdparty/cnrbtree.h
	wget https://raw.githubusercontent.com/kuba--/zip/master/src/zip.c -O thirdparty/zip.c
	wget https://raw.githubusercontent.com/kuba--/zip/master/src/zip.h -O thirdparty/zip.h
	wget https://raw.githubusercontent.com/kuba--/zip/master/src/miniz.h -O thirdparty/miniz.h

prerequisites :
	sudo apt-get install build-essential git wget


# The bible
tmp/eng-web_readaloud.zip :
	mkdir -p tmp
	wget https://ebible.org/Scriptures/eng-web_readaloud.zip -P tmp/

versemaker : versemaker.c
	gcc -g -o $@ $^

assets/bible_data.png : versemaker tmp/eng-web_readaloud.zip tmp/cross_references.txt
	./$< tmp/eng-web_readaloud.zip outfile.tsv tmp/cross_references.txt

#Topical bible: should we instead use raw word scores?  Or the other file?

tmp/cross-references.zip :
	mkdir -p tmp
	wget https://a.openbible.info/data/cross-references.zip -P tmp/

tmp/cross_references.txt : tmp/cross-references.zip
	unzip -o $< -d tmp/
	touch $@

tmp/topic-scores.zip : 
	mkdir -p tmp
	wget https://a.openbible.info/data/topic-scores.zip -P tmp/

tmp/topic-scores.txt : tmp/topic-scores.zip
	unzip -o $< -d tmp/
	touch $@
	

#cross_reference_processor : cross_reference_processor.c
#	gcc -g -o $@ $^
#assets/truncated_cross_references.png : cross_reference_processor tmp/cross_references.txt
#	./$< tmp/cross_references.txt

everything : assets/bible_data.png

clobber :
	rm -rf tmp
	rm -rf assets

clean :
	rm -rf cross_reference_processor versemaker tmp/cross_references.txt
