#ifndef SDF_INC
#define SDF_INC

void sdf_load(int drive, const char *fn);
FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize);

#endif
