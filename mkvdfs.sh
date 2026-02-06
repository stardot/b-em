#! /bin/sh

ver=$(utils/b-em-version-str.sh)
echo "EQUS \"$ver\":EQUB 0" > src/version.asm
cd src && beebasm -v -i vdfs.asm > vdfs.lst
