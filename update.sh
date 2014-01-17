#!/bin/sh

# resync this library with the upstream project

rm -fr statsite
git clone https://github.com/armon/statsite.git

for i in deps/murmurhash deps/inih src; do
	cp statsite/$i/*.c src
	cp statsite/$i/*.h include/statsite
done

for i in conn_handler.c networking.c statsite.c streaming.c; do
	rm src/$i;
done

pushd include
cp statsite.h.tpl statsite.h
for i in statsite/*.h; do echo "#include <$i>" >> statsite.h; done
popd

rm -fr statsite
