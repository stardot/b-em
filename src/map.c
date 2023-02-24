#include "map.h"
#include <stdlib.h>

#define HASH_TABLE_SIZE 65536
#define hash(x) (((x)&0xffff)^(((x)>>8)&0xffff)^(((x)>>16)&0xffff)^(((x)>>24)&0xffff))

typedef struct MAP_hashentry
{
  struct MAP_hashentry *next;
  int first;
  int second;
}
MAP_Hashentry;

MAP_Hashentry *MAP_lookupbyfirst[HASH_TABLE_SIZE];
MAP_Hashentry *MAP_lookupbysecond[HASH_TABLE_SIZE];

void
MAP_addtolist (MAP_Hashentry ** add, int first, int second)
{
  while (*add)
    add = &((*add)->next);
  /* Malloc will never fail? :o( */
  (*add) = (MAP_Hashentry *) malloc (sizeof (MAP_Hashentry));
  (*add)->next = (MAP_Hashentry *) 0;
  (*add)->first = first;
  (*add)->second = second;
}

void
MAP_killwholelist (MAP_Hashentry * p)
{
  MAP_Hashentry *q;

  while (p)
    {
      q = p;
      p = p->next;
      free (q);
    }
}

static void
MAP_removefromlist (MAP_Hashentry ** p, int first)
{
  MAP_Hashentry *q;

  while (*p)
    {
      if ((*p)->first == first)
	{
	  q = (*p)->next;
	  free (*p);
	  *p = q;
	  return;
	}
      p = &((*p)->next);
    }
}

void
MAP_putpair (int first, int second)
{
  int junk;

  if (MAP_getfirst (&junk, second) != MAP_NO_SUCH_PAIR)
    MAP_killpair_bysecond (second);
  MAP_addtolist (&MAP_lookupbyfirst[hash (first)], first, second);
  MAP_addtolist (&MAP_lookupbysecond[hash (second)], first, second);
}

MAP_Error
MAP_getfirst (int *first, int second)
{
  MAP_Hashentry *look;

  look = MAP_lookupbysecond[hash (second)];
  while (look)
    if (look->second == second)
      {
	*first = look->first;
	return MAP_NO_ERROR;
      }
  return MAP_NO_SUCH_PAIR;
}

MAP_Error
MAP_getsecond (int first, int *second)
{
  MAP_Hashentry *look;

  look = MAP_lookupbyfirst[hash (first)];
  while (look)
    {
      if (look->first == first)
	{
	  *second = look->second;
	  return MAP_NO_ERROR;
	}
      look = look->next;
    }
  return MAP_NO_SUCH_PAIR;
}

MAP_Error
MAP_killpair_byfirst (int first)
{
  int second;

  if (MAP_getsecond (first, &second) == MAP_NO_SUCH_PAIR)
    return MAP_NO_SUCH_PAIR;
  MAP_removefromlist (&MAP_lookupbyfirst[hash (first)], first);
  MAP_removefromlist (&MAP_lookupbysecond[hash (second)], first);
  return MAP_NO_ERROR;
}

MAP_Error
MAP_killpair_bysecond (int second)
{
  int first;

  if (MAP_getfirst (&first, second) == MAP_NO_SUCH_PAIR)
    return MAP_NO_SUCH_PAIR;
  MAP_removefromlist (&MAP_lookupbyfirst[hash (first)], first);
  MAP_removefromlist (&MAP_lookupbysecond[hash (second)], first);
  return MAP_NO_ERROR;
}

void
MAP_newmap ()
{
  int i;

  for (i = 0; i < HASH_TABLE_SIZE; i++)
    {
      MAP_killwholelist (MAP_lookupbyfirst[i]);
      MAP_killwholelist (MAP_lookupbysecond[i]);
      MAP_lookupbyfirst[i] = MAP_lookupbysecond[i] = (MAP_Hashentry *) 0;
    }
}
