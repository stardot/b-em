SET VERSION=DEV
set CXX=g++
set CC=gcc
set WINDRES=windres
set ALLEGRO_BASE=../../allegro5
set ALLEGRO_INC=-I %ALLEGRO_BASE%/include
set ALLEGRO_LIB=-L %ALLEGRO_BASE%/lib -L %ALLEGRO_BASE%/bin

cd src
mingw32-make -j4 -e -f Makefile.win
cd ..
