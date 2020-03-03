#ifndef SDF_INC
#define SDF_INC

enum sdf_sides {
    SDF_SIDES_SINGLE,
    SDF_SIDES_SEQUENTIAL,
    SDF_SIDES_INTERLEAVED
};

enum sdf_density {
    SDF_DENS_NA,
    SDF_DENS_SINGLE,
    SDF_DENS_DOUBLE,
    SDF_DENS_QUAD
};

enum sdf_disc_type {
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
};

struct sdf_geometry {
    const char       *name;
    enum sdf_sides   sides;
    enum sdf_density density;
    uint8_t          tracks;
    uint8_t          sectors_per_track;
    uint16_t         sector_size;
    void (*new_disc)(FILE *f, const struct sdf_geometry *geo);
};

extern struct sdf_geometry sdf_geo_tab[];
extern char *mmb_fn;

// In sdf-geo.c
const struct sdf_geometry *sdf_find_geo(const char *fn, const char *ext, FILE *fp);
const char *sdf_desc_sides(const struct sdf_geometry *geo);
const char *sdf_desc_dens(const struct sdf_geometry *geo);
struct sdf_geometry *sdf_create_disc(const char *fn, enum sdf_disc_type dtype);

void sdf_new_disc(int drive, ALLEGRO_PATH *fn, enum sdf_disc_type type);
void sdf_load(int drive, const char *fn, const char *ext);
FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize);

// Functions for MMB files.
void mmb_load(char *fn);
void mmb_eject(void);
void mmb_pick(int drive, int disc);
void mmb_reset(void);
int mmb_find(const char *name);


//DB: bodge for VS
#ifdef _MSC_VER 
//not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif


#endif
