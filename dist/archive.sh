#!/bin/bash
set -e
FILENAME=dist/osmexpress-$1-$2.tgz
rm -f LICENSES
printf "osmexpress\n===========\n" >> LICENSES
cat LICENSE.md >> LICENSES
printf "\ncapnproto\n===========\n" >> LICENSES
cat vendor/capnproto/LICENSE >> LICENSES
printf "\ncroaring\n===========\n" >> LICENSES
cat vendor/CRoaring/LICENSE >> LICENSES
printf "\ncxxopts\n===========\n" >> LICENSES
cat vendor/cxxopts/LICENSE >> LICENSES
printf "\njson\n===========\n" >> LICENSES
cat vendor/json/LICENSE.MIT >> LICENSES
printf "\nlibosmium\n===========\n" >> LICENSES
cat vendor/libosmium/LICENSE >> LICENSES
printf "\nlmdb\n===========\n" >> LICENSES
cat vendor/lmdb/libraries/liblmdb/LICENSE >> LICENSES
printf "\nprotozero\n===========\n" >> LICENSES
cat vendor/protozero/LICENSE.md >> LICENSES
printf "\ns2\n===========\n" >> LICENSES
cat vendor/s2geometry/LICENSE >> LICENSES
tar -cvzf $FILENAME osmx LICENSES
rm LICENSES
echo "created $FILENAME"
