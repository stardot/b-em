int adcconvert,adctime;
void writeadc(unsigned short addr, unsigned char val);
unsigned char readadc(unsigned short addr);
void initadc();

#define polladc(cycles)         \
        if (adcconvert)         \
        {                       \
                adctime-=cycles;\
                if (adctime<0)  \
                   adcpoll();   \
        }
