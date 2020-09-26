#ifndef __INC_COMPATCMOS_H
#define __INC_COMPATCMOS_H

void compactcmos_load(const MODEL *m);
void compactcmos_save(const MODEL *m);
void compactcmos_i2cchange(int nuclock, int nudata);

extern int i2c_clock, i2c_data;

#endif
