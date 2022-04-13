#ifndef PG_DIFFIX_AID_TRACKER_H
#define PG_DIFFIX_AID_TRACKER_H

#include "nodes/pg_list.h"

#include "pg_diffix/aggregation/aid.h"
#include "pg_diffix/aggregation/common.h"
#include "pg_diffix/aggregation/noise.h"

typedef struct AidTrackerHashEntry
{
  aid_t aid;   /* Entry key */
  char status; /* Required for hash table */
} AidTrackerHashEntry;

/*
 * Declarations for HashTable<aid_t, AidTrackerHashEntry>
 */
#define SH_PREFIX AidTracker
#define SH_ELEMENT_TYPE AidTrackerHashEntry
#define SH_KEY_TYPE aid_t
#define SH_SCOPE extern
#define SH_DECLARE
#include "lib/simplehash.h"

typedef struct AidTrackerState
{
  MakeAidFunc aid_maker;    /* Creator of AIDs from Datums */
  AidTracker_hash *aid_set; /* Hash set of all AIDs */
  seed_t aid_seed;          /* Current AID seed */
} AidTrackerState;

/*
 * Updates state with an AID.
 */
extern void aid_tracker_update(AidTrackerState *state, aid_t aid);

/*
 * Creates the multi-AID aggregation state from the function arguments.
 */
extern List *create_aid_trackers(ArgsDescriptor *args_desc, int aids_offset);

#endif /* PG_DIFFIX_AID_TRACKER_H */
