#!/bin/sh

# resync this library with the upstream project

rm -fr statsite
git clone https://github.com/armon/statsite.git

VERS=`head -n1 statsite/CHANGELOG.md |cut -f2 -d' '`

echo "AC_INIT([libstatsite], [${VERS}])" > configure.ac
cat configure.ac.tpl >> configure.ac

for i in deps/murmurhash deps/inih src; do
	cp statsite/$i/*.c src
	cp statsite/$i/*.h include/statsite
done

for i in conn_handler networking statsite streaming; do
	rm -f src/$i.c;
	rm -f include/statsite/$i.h
done

pushd src
	cp Makefile.am.tpl Makefile.am
	for i in *.c; do
		echo "libstatsite_la_SOURCES += $i" >> Makefile.am
	done
popd

pushd include
	cp statsite.h.tpl statsite.h
	for i in statsite/*.h; do
		echo "#include <$i>" >> statsite.h;
	done
	pushd statsite
		cp Makefile.am.tpl Makefile.am
		for i in *.h; do
			echo "statsiteinclude_HEADERS += $i" >> Makefile.am
		done
	popd
popd

rm -fr statsite
