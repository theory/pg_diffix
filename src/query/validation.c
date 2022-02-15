#include "postgres.h"

#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "optimizer/tlist.h"

#include "pg_diffix/config.h"
#include "pg_diffix/oid_cache.h"
#include "pg_diffix/query/allowed_functions.h"
#include "pg_diffix/query/validation.h"
#include "pg_diffix/utils.h"

#define NOT_SUPPORTED(cond, feature) \
  if (cond)                          \
    FAILWITH("Feature '%s' is not currently supported.", (feature));

static void verify_query(Query *query);
static void verify_rtable(Query *query);
static void verify_aggregators(Query *query);
static void verify_bucket_functions(Query *query);

void verify_anonymization_requirements(Query *query)
{
  /*
   * No easy way to fully check these related parameters using GUC. If someone manages to misconfigure, we need to fail
   * here.
   */
  config_check();
  verify_query(query);
}

static void verify_query(Query *query)
{
  NOT_SUPPORTED(query->commandType != CMD_SELECT, "non-select query");
  NOT_SUPPORTED(query->cteList, "WITH");
  NOT_SUPPORTED(query->hasForUpdate, "FOR [KEY] UPDATE/SHARE");
  NOT_SUPPORTED(query->hasSubLinks, "SubLinks");
  NOT_SUPPORTED(query->hasTargetSRFs, "SRF functions");
  NOT_SUPPORTED(query->groupingSets, "GROUPING SETS");
  NOT_SUPPORTED(query->windowClause, "window functions");
  NOT_SUPPORTED(query->distinctClause, "DISTINCT");
  NOT_SUPPORTED(query->setOperations, "UNION/INTERSECT");

  verify_aggregators(query);
  verify_bucket_functions(query);
  verify_rtable(query);
}

static void verify_rtable(Query *query)
{
  /* Cater for cross joins in the form of `FROM from_item1, from_item2, ...`. */
  NOT_SUPPORTED(list_length(query->rtable) > 1, "JOINs in anonymizing queries");

  ListCell *cell = NULL;
  foreach (cell, query->rtable)
  {
    RangeTblEntry *range_table = lfirst_node(RangeTblEntry, cell);
    NOT_SUPPORTED(range_table->rtekind == RTE_SUBQUERY, "Subqueries in anonymizing queries");
    NOT_SUPPORTED(range_table->rtekind == RTE_JOIN, "JOINs in anonymizing queries");

    if (range_table->rtekind != RTE_RELATION)
      FAILWITH("Unsupported FROM clause.");
  }
}

static bool verify_aggregator(Node *node, void *context)
{
  if (node == NULL)
    return false;

  if (IsA(node, Aggref))
  {
    Aggref *aggref = (Aggref *)node;
    Oid aggoid = aggref->aggfnoid;

    if (aggoid != g_oid_cache.count && aggoid != g_oid_cache.count_any)
      FAILWITH_LOCATION(aggref->location, "Unsupported aggregate in query.");
  }

  return expression_tree_walker(node, verify_aggregator, context);
}

static void verify_aggregators(Query *query)
{
  query_tree_walker(query, verify_aggregator, NULL, 0);
}

static bool verify_bucket_function(Node *node, void *context)
{
  if (node == NULL)
    return false;

  if (IsA(node, FuncExpr))
  {
    FuncExpr *funcref = (FuncExpr *)node;
    Oid funcoid = funcref->funcid;

    if (!is_allowed_cast(funcoid) && !is_allowed_function(funcoid))
      FAILWITH_LOCATION(funcref->location, "Unsupported function used to define buckets.");
  }

  if (IsA(node, OpExpr))
  {
    OpExpr *funcref = (OpExpr *)node;
    /* We don't yet support any operators. */
    FAILWITH_LOCATION(funcref->location, "Unsupported operator used to define buckets.");
  }

  return expression_tree_walker(node, verify_bucket_function, context);
}

static void verify_bucket_functions(Query *query)
{
  List *exprs_list = NIL;
  if (query->groupClause != NIL)
    /* Buckets are explicitly defined. */
    exprs_list = get_sortgrouplist_exprs(query->groupClause, query->targetList);
  else if (!query->hasAggs)
    /* Buckets are implicitly defined. */
    exprs_list = get_tlist_exprs(query->targetList, false);
  /* Else we have the global bucket, nothing to check. */

  ListCell *cell;
  foreach (cell, exprs_list)
  {
    Node *expr = (Node *)lfirst(cell);
    verify_bucket_function(expr, NULL);
  }
}
