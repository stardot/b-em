#ifndef __INC_TAPENOISE_H
#define __INC_TAPENOISE_H

void tapenoise_init();
void tapenoise_close();
void tapenoise_addhigh();
void tapenoise_adddat(uint8_t dat);
void tapenoise_motorchange(int stat);
void tapenoise_mix(int16_t *tapebuffer);

#endif
