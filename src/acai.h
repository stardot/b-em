unsigned char acaicr; /*Control register*/
unsigned char acaisr; /*Status register*/
unsigned char acaidr; /*Data register*/
int acaidrf;          /*Data register full?*/

unsigned char readacai(unsigned short addr);
void writeacai(unsigned short addr, unsigned char val);
void writetoacai(unsigned char val);
