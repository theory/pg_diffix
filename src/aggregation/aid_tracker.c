#include "postgres.h"
#include "common/hashfn.h"

#include "pg_diffix/utils.h"
#include "pg_diffix/aggregation/aid_tracker.h"

/*
 * Definitions for HashTable<aid_t, AidTrackerHashEntry>
 */
#define SH_PREFIX AidTracker
#define SH_ELEMENT_TYPE AidTrackerHashEntry
#define SH_KEY aid
#define SH_KEY_TYPE aid_t
#define SH_EQUAL(tb, a, b) a == b
#define SH_HASH_KEY(tb, key) HASH_AID_32(key)
#define SH_SCOPE inline
#define SH_DEFINE
#include "lib/simplehash.h"

static AidTrackerState *aid_tracker_new(
    AidDescriptor aid_descriptor,
    uint64 initial_seed)
{
  AidTrackerState *state = (AidTrackerState *)palloc0(sizeof(AidTrackerState));

  state->aid_descriptor = aid_descriptor;
  state->aid_set = AidTracker_create(128, NULL);
  state->aid_seed = initial_seed;

  return state;
}

void aid_tracker_update(AidTrackerState *state, aid_t aid)
{
  bool found;
  AidTracker_insert(state->aid_set, aid, &found);
  if (!found)
  {
    state->aid_seed ^= state->aid_descriptor.is_hash ? aid : HASH_AID_64(aid);
  }
}

static const int STATE_INDEX = 0;
static const int AID_INDEX = 1;

AidTrackerState *get_aggregate_aid_tracker(PG_FUNCTION_ARGS)
{
  if (!PG_ARGISNULL(STATE_INDEX))
    return (AidTrackerState *)PG_GETARG_POINTER(STATE_INDEX);

  MemoryContext agg_context;
  if (AggCheckCallContext(fcinfo, &agg_context) != AGG_CONTEXT_AGGREGATE)
    FAILWITH("Aggregate called in non-aggregate context");

  /* We want all memory allocations to be done per aggregation node. */
  MemoryContext old_context = MemoryContextSwitchTo(agg_context);

  Oid aid_type = get_fn_expr_argtype(fcinfo->flinfo, AID_INDEX);
  AidTrackerState* tracker = aid_tracker_new(get_aid_descriptor(aid_type), 0);

  MemoryContextSwitchTo(old_context);
  return tracker;
}

List *get_aggregate_aid_trackers(PG_FUNCTION_ARGS, int aids_offset)
{
  if (!PG_ARGISNULL(STATE_INDEX))
    return (List *)PG_GETARG_POINTER(STATE_INDEX);

  MemoryContext agg_context;
  if (AggCheckCallContext(fcinfo, &agg_context) != AGG_CONTEXT_AGGREGATE)
    FAILWITH("Aggregate called in non-aggregate context");

  Assert(PG_NARGS() > aids_offset);

  /* We want all memory allocations to be done per aggregation node. */
  MemoryContext old_context = MemoryContextSwitchTo(agg_context);

  List *trackers = NIL;
  for (int arg_index = aids_offset; arg_index < PG_NARGS(); arg_index++)
  {
    Oid aid_type = get_fn_expr_argtype(fcinfo->flinfo, arg_index);
    AidTrackerState *tracker = aid_tracker_new(get_aid_descriptor(aid_type), 0);
    trackers = lappend(trackers, tracker);
  }

  MemoryContextSwitchTo(old_context);
  return trackers;
}
