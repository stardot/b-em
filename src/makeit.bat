gcc -c 6502.c
gcc -c 8271.c
gcc -c adc.c
gcc -c bem.c
gcc -c disk.c
gcc -c gui.c
gcc -c mem.c
gcc -c snapshot.c
gcc -c sound.c
gcc -c vias.c
gcc -c video.c
gcc -o b-em.exe 6502.o 8271.o adc.o bem.o disk.o gui.o mem.o snapshot.o sound.o vias.o video.o -lalleg
gcc -c playsn.c
gcc -o playsn.exe playsn.o -lalleg
