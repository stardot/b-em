#ifndef SDF_INC
#define SDF_INC

typedef enum {
    SDF_FMT_DFS_SINGLE,
    SDF_FMT_DFS_SEQUENTIAL,
    SDF_FMT_DFS_INTERLEAVED,
    SDF_FMT_ADFS_SEQ_SMALL,
    SDF_FMT_ADFS_SEQ_LARGE,
    SDF_FMT_ADFS_INTERLEAVED,
    SDF_FMT_DDFS_SINGLE_16S,
    SDF_FMT_DDFS_SINGLE_18S,
    SDF_FMT_DDFS_INTERLEAVED_16S,
    SDF_FMT_DDFS_INTERLEAVED_18S,
    SDF_FMT_DOS720K,
    SDF_FMT_DOS360K,
    SDF_FMT_MAX
} sdf_disc_type;

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, sdf_disc_type type);
void sdf_load(int drive, const char *fn, const char *ext);
FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize);

#endif
