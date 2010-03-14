uint8_t aciacr; /*Control register*/
uint8_t aciasr; /*Status register*/
uint8_t aciadr; /*Data register*/
int aciadrf;          /*Data register full?*/

uint8_t readacia(uint16_t addr);
void writeacia(uint16_t addr, uint8_t val);
void writetoacia(uint8_t val);
