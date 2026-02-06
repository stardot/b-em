#! /bin/sh

set -e
VERSION=$(utils/b-em-version-str.sh)
export VERSION
export VPATH='../src ../src/resid-fp ../src/NS32016 ../src/darm ../src/mc6809nc ../src/pdp11 ../src/musahi ../src/ARMulator'

dlls='allegro-5.2.dll             allegro_acodec-5.2.dll
      allegro_audio-5.2.dll       allegro_dialog-5.2.dll
      allegro_font-5.2.dll        allegro_image-5.2.dll
      allegro_primitives-5.2.dll  libstdc++-6.dll
      libwebp-7.dll               libwinpthread-1.dll
      OpenAL32.dll                zlib1.dll
      libsharpyuv-0.dll           libssp-0.dll'

dirs='ddnoise discs docs fonts icon roms tapes'
cmos='cmos350.bin cmosa.bin cmos.bin cmosc.bin'

buildit() {
    export CC="$2-gcc"
    export CXX="$2-g++"
    export WINDRES="$2-windres"
    gcc_ver=$($CC -dumpversion)

    dir="build-$1"
    if [ ! -d "$dir" ]
    then
        mkdir "$dir"
        for dll in $dlls $3
        do
            test -f "/usr/$2/bin/$dll" && ln -s "/usr/$2/bin/$dll" "$dir"
            test -f "/usr/$2/lib/$dll" && ln -s "/usr/$2/lib/$dll" "$dir"
            test -f "/usr/lib/gcc/$2/$gcc_ver/$dll" && ln -s "/usr/lib/gcc/$2/$gcc_ver/$dll" "$dir"
        done
        for item in $dirs b-em.cfg $cmos
        do
            ln -s "../$item" "$dir/$item"
        done
    fi

    cd "$dir"
    make -j4 -e -f ../src/Makefile.win b-em.exe
    zip -q -r "b-em-$VERSION-$1.zip" *.exe b-em.cfg *.dll $dirs
    cd ..
}

buildit w32 i686-w64-mingw32 libgcc_s_dw2-1.dll &
buildit w64 x86_64-w64-mingw32 libgcc_s_seh-1.dll &

wait
