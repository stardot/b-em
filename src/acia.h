unsigned char aciacr; /*Control register*/
unsigned char aciasr; /*Status register*/
unsigned char aciadr; /*Data register*/
int aciadrf;          /*Data register full?*/

unsigned char readacia(unsigned short addr);
void writeacia(unsigned short addr, unsigned char val);
void writetoacia(unsigned char val);
