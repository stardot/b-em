#ifndef SS_INLINE_INC
#define SS_INLINE_INC

static inline unsigned char *save_uint16(unsigned char *ptr, uint16_t val)
{
    *ptr++ = val; val >>= 8;
    *ptr++ = val;
    return ptr;
}

static inline unsigned char *save_uint32(unsigned char *ptr, uint32_t val)
{
    *ptr++ = val; val >>= 8;
    *ptr++ = val; val >>= 8;
    *ptr++ = val; val >>= 8;
    *ptr++ = val;
    return ptr;
}

static inline unsigned char *load_uint16(unsigned char *ptr, uint16_t *dest)
{
    uint16_t v;
    v = *ptr++;
    v |= (*ptr++) <<  8;
    *dest = v;
    return ptr;
}

static unsigned char *load_uint32(unsigned char *ptr, uint32_t *dest)
{
    uint32_t v;
    v = *ptr++;
    v |= (*ptr++) <<  8;
    v |= (*ptr++) << 16;
    v |= (*ptr++) << 24;
    *dest = v;
    return ptr;
}

#endif
