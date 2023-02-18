typedef enum
{
  NO_ERROR,
  DELETED_OLD_PAIR,
  NO_SUCH_PAIR,
}
MAP_Error;

void MAP_putpair (int first, int second);

void MAP_newmap (void);
MAP_Error MAP_killpair_byfirst (int first);
MAP_Error MAP_killpair_bysecond (int second);

MAP_Error MAP_getfirst (int *first, int second);
MAP_Error MAP_getsecond (int first, int *second);
