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

struct sdf_geometry {
    const char       *name;
    enum sdf_sides   sides;
    enum sdf_density density;
    uint8_t          tracks;
    uint8_t          sectors_per_track;
    uint16_t         sector_size;
    void (*new_disc)(FILE *f, const struct sdf_geometry *geo);
};

struct sdf_geometry_set {
    struct sdf_geometry adfs_s;
    struct sdf_geometry adfs_m;
    struct sdf_geometry adfs_l;
    struct sdf_geometry adfs_d;
    struct sdf_geometry dfs_10s_sin_40t;
    struct sdf_geometry dfs_10s_int_40t;
    struct sdf_geometry dfs_10s_sin_80t;
    struct sdf_geometry dfs_10s_int_80t;
    struct sdf_geometry dfs_10s_seq_80t;
    struct sdf_geometry dfs_16s_sin_40t;
    struct sdf_geometry dfs_16s_int_40t;
    struct sdf_geometry dfs_16s_sin_80t;
    struct sdf_geometry dfs_16s_int_80t;
    struct sdf_geometry dfs_16s_seq_80t;
    struct sdf_geometry dfs_18s_sin_40t;
    struct sdf_geometry dfs_18s_int_40t;
    struct sdf_geometry dfs_18s_sin_80t;
    struct sdf_geometry dfs_18s_int_80t;
    struct sdf_geometry dfs_18s_seq_80t;
    struct sdf_geometry dos_720k;
    struct sdf_geometry dos_360k;
};

extern const struct sdf_geometry_set sdf_geometries;

extern FILE *sdf_fp[];

// In sdf-geo.c
const struct sdf_geometry *sdf_find_geo(const char *fn, const char *ext, FILE *fp);
const char *sdf_desc_sides(const struct sdf_geometry *geo);
const char *sdf_desc_dens(const struct sdf_geometry *geo);

// In sdf-acc.c
void sdf_new_disc(int drive, ALLEGRO_PATH *fn, const struct sdf_geometry *geo);
void sdf_mount(int drive, const char *fn, FILE *fp, const struct sdf_geometry *geo);
void sdf_load(int drive, const char *fn, const char *ext);
FILE *sdf_owseek(uint8_t drive, uint8_t sector, uint8_t track, uint8_t side, uint16_t ssize);

//DB: bodge for VS
#ifdef _MSC_VER
//not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

#endif
