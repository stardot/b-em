gcc -c -O1 6850acia.c
gcc -c -O1 8271.c
gcc -c -O1 adc.c
gcc -c     bem.c
gcc -c -O1 disk.c
gcc -c -g  mem.c
gcc -c -O1 serial.c
gcc -c -O1 snapshot.c
gcc -c -O1 sound.c
gcc -c -O1 uef.c
gcc -c -O1 video.c
gcc -c -O1 vias.c
gcc -o b-em.exe 6850acia.o 8271.o adc.o bem.o disk.o mem.o serial.o snapshot.o sound.o uef.o video.o vias.o gfx.a
gcc -c setup.c
gcc -o setup.exe setup.o
