@echo off

cd src
make -f Makefile.win b-em.res
make -f Makefile.win
cd ..
