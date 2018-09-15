#ifndef SDF_INC
#define SDF_INC

typedef enum {
    SDF_FMT_ADFS_S,
    SDF_FMT_ADFS_M,
    SDF_FMT_ADFS_L,
    SDF_FMT_ADFS_D,
    SDF_FMT_DFS_10S_SIN_40T,
    SDF_FMT_DFS_10S_INT_40T,
    SDF_FMT_DFS_10S_SEQ_40T,
    SDF_FMT_DFS_10S_SIN_80T,
    SDF_FMT_DFS_10S_INT_80T,
    SDF_FMT_DFS_10S_SEQ_80T,
    SDF_FMT_DFS_16S_SIN_80T,
    SDF_FMT_DFS_16S_INT_80T,
    SDF_FMT_DFS_16S_SEQ_80T,
    SDF_FMT_DFS_18S_SIN_80T,
    SDF_FMT_DFS_18S_INT_80T,
    SDF_FMT_DFS_18S_SEQ_80T,
    SDF_FMT_DOS720K,
    SDF_FMT_DOS360K,
    SDF_FMT_MAX
} sdf_disc_type;

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, sdf_disc_type type);
void sdf_load(int drive, const char *fn, const char *ext);
FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize);

#endif
