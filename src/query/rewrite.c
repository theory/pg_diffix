#include "postgres.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/parse_oper.h"
#include "catalog/pg_type.h"
#include "catalog/pg_aggregate.h"

#include "pg_diffix/utils.h"
#include "pg_diffix/query/oid_cache.h"
#include "pg_diffix/query/relation.h"
#include "pg_diffix/query/rewrite.h"

typedef struct AidReference
{
  SensitiveRelation *relation; /* Source relation of AID */
  AnonymizationID *aid;        /* Column data for AID */
  Index rte_index;             /* RTE index in query rtable */
  AttrNumber aid_attnum;       /* AID AttrNumber in relation/subquery */
} AidReference;

typedef struct QueryContext
{
  Query *query;         /* Current query/subquery */
  List *aid_references; /* `AidReference`s in scope */
  List *child_contexts; /* `QueryContext`s of subqueries */
} QueryContext;

/* Mutators */
static void group_and_expand_implicit_buckets(Query *query);
static Node *aggregate_expression_mutator(Node *node, QueryContext *context);
static void add_low_count_filter(QueryContext *context);

/* AID Utils */
static QueryContext *build_context(Query *query, List *relations);
static void append_aid_args(Aggref *aggref, QueryContext *context);

/*-------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------
 */

void rewrite_query(Query *query, List *sensitive_relations)
{
  QueryContext *context = build_context(query, sensitive_relations);

  group_and_expand_implicit_buckets(query);

  query_tree_mutator(
      query,
      aggregate_expression_mutator,
      context,
      QTW_DONT_COPY_QUERY);

  add_low_count_filter(context);

  query->hasAggs = true; /* Anonymizing queries always have at least one aggregate. */
}

/*-------------------------------------------------------------------------
 * Implicit grouping
 *-------------------------------------------------------------------------
 */

static void group_implicit_buckets(Query *query)
{
  ListCell *lc = NULL;
  foreach (lc, query->targetList)
  {
    TargetEntry *tle = lfirst_node(TargetEntry, lc);

    Oid type = exprType((const Node *)tle->expr);
    Assert(type != UNKNOWNOID);

    /* Set group index to ordinal position. */
    tle->ressortgroupref = tle->resno;

    /* Determine the eqop and optional sortop. */
    Oid sortop = 0;
    Oid eqop = 0;
    bool hashable = false;
    get_sort_group_operators(type, false, true, false, &sortop, &eqop, NULL, &hashable);

    /* Create group clause for current item. */
    SortGroupClause *groupClause = makeNode(SortGroupClause);
    groupClause->tleSortGroupRef = tle->ressortgroupref;
    groupClause->eqop = eqop;
    groupClause->sortop = sortop;
    groupClause->nulls_first = false; /* OK with or without sortop */
    groupClause->hashable = hashable;

    /* Add group clause to query. */
    query->groupClause = lappend(query->groupClause, groupClause);
  }
}

/*
 * Expand implicit buckets by adding a hidden call to `generate_series(1, anon_count(*))` to the selection list.
 */
static void expand_implicit_buckets(Query *query)
{
  Const *const_one = makeConst(INT8OID, -1, InvalidOid, SIZEOF_LONG, Int64GetDatum(1), false, FLOAT8PASSBYVAL);

  Aggref *count_agg = makeNode(Aggref);
  count_agg->aggfnoid = g_oid_cache.count; /* Will be replaced later with anonymizing version. */
  count_agg->aggtype = INT8OID;
  count_agg->aggtranstype = InvalidOid; /* Will be set by planner. */
  count_agg->aggstar = true;
  count_agg->aggvariadic = false;
  count_agg->aggkind = AGGKIND_NORMAL;
  count_agg->aggsplit = AGGSPLIT_SIMPLE; /* Planner might change this. */
  count_agg->location = -1;              /* Unknown location. */

  FuncExpr *generate_series = makeNode(FuncExpr);
  generate_series->funcid = g_oid_cache.generate_series;
  generate_series->funcresulttype = INT8OID;
  generate_series->funcretset = true;
  generate_series->funcvariadic = false;
  generate_series->args = list_make2(const_one, count_agg);
  generate_series->location = -1;

  int target_count = list_length(query->targetList);
  TargetEntry *expand_entry = makeTargetEntry((Expr *)generate_series, target_count + 1, NULL, false);
  expand_entry->resjunk = true; /* Hide output values. */

  lappend(query->targetList, expand_entry);
  query->hasTargetSRFs = true;
}

static void group_and_expand_implicit_buckets(Query *query)
{
  /* Only simple select queries require implicit grouping. */
  if (query->hasAggs || query->groupClause != NIL)
    return;

  DEBUG_LOG("Rewriting query to group and expand implicit buckets (Query ID=%lu).", query->queryId);

  group_implicit_buckets(query);
  expand_implicit_buckets(query);
}

/*-------------------------------------------------------------------------
 * Low count filtering
 *-------------------------------------------------------------------------
 */

static void add_low_count_filter(QueryContext *context)
{
  Query *query = context->query;
  /* Global aggregates have to be excluded from low-count filtering. */
  if (query->hasAggs && query->groupClause == NIL)
    return;

  Aggref *lcf_agg = makeNode(Aggref);

  lcf_agg->aggfnoid = g_oid_cache.lcf;
  lcf_agg->aggtype = BOOLOID;
  lcf_agg->aggtranstype = InvalidOid; /* Will be set by planner. */
  lcf_agg->aggstar = false;
  lcf_agg->aggvariadic = false;
  lcf_agg->aggkind = AGGKIND_NORMAL;
  lcf_agg->aggsplit = AGGSPLIT_SIMPLE; /* Planner might change this. */
  lcf_agg->location = -1;              /* Unknown location. */

  append_aid_args(lcf_agg, context);

  query->havingQual = make_and_qual(query->havingQual, (Node *)lcf_agg);
  query->hasAggs = true;
}

/*-------------------------------------------------------------------------
 * Anonymizing aggregates
 *-------------------------------------------------------------------------
 */

/*
 * Resolves `rel_oid` and `attnum` if target entry is a simple reference to a table column.
 * Returns false if target entry is not a simple reference.
 */
static bool find_source_column(
    const Query *query,
    const TargetEntry *te,
    Oid *rel_oid,
    AttrNumber *attnum)
{
  if (OidIsValid(te->resorigtbl) && AttributeNumberIsValid(te->resorigcol))
  {
    *rel_oid = te->resorigtbl;
    *attnum = te->resorigcol;
    return true;
  }

  if (!IsA(te->expr, Var))
    return false;

  Var *var = (Var *)te->expr;
  RangeTblEntry *rte = list_nth(query->rtable, var->varno - 1);

  if (rte->rtekind == RTE_RELATION)
  {
    *rel_oid = rte->relid;
    *attnum = var->varattno;
    return true;
  }
  else if (rte->rtekind == RTE_SUBQUERY)
    return find_source_column(
        rte->subquery,
        list_nth(rte->subquery->targetList, var->varattno - 1),
        rel_oid,
        attnum);
  else
    return false;
}

/* Returns true if the target entry is an unmodified reference to an AID. */
static bool is_aid_arg(TargetEntry *arg, QueryContext *context)
{
  Oid rel_oid;
  AttrNumber attnum;

  if (!find_source_column(context->query, arg, &rel_oid, &attnum))
    return false;

  ListCell *lc;
  foreach (lc, context->aid_references)
  {
    AidReference *aid_ref = (AidReference *)lfirst(lc);
    if (rel_oid == aid_ref->relation->oid && attnum == aid_ref->aid->attnum)
      return true;
  }

  return false;
}

static void rewrite_count(Aggref *aggref, QueryContext *context)
{
  aggref->aggfnoid = g_oid_cache.anon_count;
  aggref->aggstar = false;
  append_aid_args(aggref, context);
}

static void rewrite_count_distinct(Aggref *aggref, QueryContext *context)
{
  aggref->aggfnoid = g_oid_cache.anon_count_distinct;
  /* The UDF handles distinct counting internally */
  aggref->aggdistinct = false;
  TargetEntry *arg = linitial_node(TargetEntry, aggref->args);
  if (!is_aid_arg(arg, context))
  {
    FAILWITH_LOCATION(
        exprLocation((Node *)arg->expr),
        "COUNT(DISTINCT col) requires an AID column as its argument.");
  }
}

static void rewrite_count_any(Aggref *aggref, QueryContext *context)
{
  aggref->aggfnoid = g_oid_cache.anon_count_any;
  append_aid_args(aggref, context);
}

static Node *aggregate_expression_mutator(Node *node, QueryContext *context)
{
  if (node == NULL)
    return NULL;

  if (IsA(node, Query))
  {
    Query *query = (Query *)node;
    return (Node *)query_tree_mutator(
        query,
        aggregate_expression_mutator,
        context,
        QTW_DONT_COPY_QUERY);
  }
  else if (IsA(node, Aggref))
  {
    /*
     * Copy and visit sub expressions.
     * We basically use this for copying, but we could use the visitor to process args in the future.
     */
    Aggref *aggref = (Aggref *)expression_tree_mutator(node, aggregate_expression_mutator, context);
    Oid aggfnoid = aggref->aggfnoid;

    if (aggfnoid == g_oid_cache.count)
      rewrite_count(aggref, context);
    else if (aggfnoid == g_oid_cache.count_any && aggref->aggdistinct)
      rewrite_count_distinct(aggref, context);
    else if (aggfnoid == g_oid_cache.count_any)
      rewrite_count_any(aggref, context);
    /*
    else
      FAILWITH("Unsupported aggregate in query.");
    */

    return (Node *)aggref;
  }

  return expression_tree_mutator(node, aggregate_expression_mutator, context);
}

/*-------------------------------------------------------------------------
 * AID Utils
 *-------------------------------------------------------------------------
 */

static Expr *make_aid_expr(AidReference *ref)
{
  return (Expr *)makeVar(
      ref->rte_index,
      ref->aid_attnum,
      ref->aid->atttype,
      ref->aid->typmod,
      ref->aid->collid,
      0);
}

static TargetEntry *make_aid_target(AidReference *ref, AttrNumber resno, bool resjunk)
{
  TargetEntry *te = makeTargetEntry(
      make_aid_expr(ref),
      resno,
      "aid",
      resjunk);

  te->resorigtbl = ref->relation->oid;
  te->resorigcol = ref->aid->attnum;

  return te;
}

static SensitiveRelation *find_relation(Oid rel_oid, List *relations)
{
  ListCell *lc;
  foreach (lc, relations)
  {
    SensitiveRelation *relation = (SensitiveRelation *)lfirst(lc);
    if (relation->oid == rel_oid)
      return relation;
  }

  return NULL;
}

/*
 * Adds references targeting AIDs of relation to `aid_references`.
 */
static void gather_relation_aids(
    const SensitiveRelation *relation,
    const Index rte_index,
    RangeTblEntry *rte,
    List **aid_references)
{
  ListCell *lc;
  foreach (lc, relation->aids)
  {
    AnonymizationID *aid = (AnonymizationID *)lfirst(lc);

    AidReference *aid_ref = palloc(sizeof(AidReference));
    aid_ref->relation = (SensitiveRelation *)relation;
    aid_ref->aid = aid;
    aid_ref->rte_index = rte_index;
    aid_ref->aid_attnum = aid->attnum;

    *aid_references = lappend(*aid_references, aid_ref);

    /* Emulate what the parser does */
    rte->selectedCols = bms_add_member(
        rte->selectedCols, aid->attnum - FirstLowInvalidHeapAttributeNumber);
  }
}

/*
 * Appends AID expressions of the subquery to its target list for use in parent query.
 * References to the (re-exported) AIDs are added to `aid_references`.
 */
static void gather_subquery_aids(
    const QueryContext *child_context,
    const Index rte_index,
    List **aid_references)
{
  Query *subquery = child_context->query;
  AttrNumber next_attnum = list_length(subquery->targetList) + 1;

  ListCell *lc;
  foreach (lc, child_context->aid_references)
  {
    AidReference *child_aid_ref = (AidReference *)lfirst(lc);

    /* Export AID from subquery */
    AttrNumber attnum = next_attnum++;
    subquery->targetList = lappend(
        subquery->targetList,
        make_aid_target(child_aid_ref, attnum, true));

    /* Path to AID from parent query */
    AidReference *parent_aid_ref = palloc(sizeof(AidReference));
    parent_aid_ref->relation = child_aid_ref->relation;
    parent_aid_ref->aid = child_aid_ref->aid;
    parent_aid_ref->rte_index = rte_index;
    parent_aid_ref->aid_attnum = attnum;

    *aid_references = lappend(*aid_references, parent_aid_ref);
  }
}

/*
 * Collects and prepares AIDs for use in the current query's scope.
 * Subqueries are rewritten to export all their AIDs.
 */
static QueryContext *build_context(Query *query, List *relations)
{
  List *aid_references = NIL;
  List *child_contexts = NIL;

  ListCell *lc;
  foreach (lc, query->rtable)
  {
    RangeTblEntry *rte = (RangeTblEntry *)lfirst(lc);
    Index rte_index = foreach_current_index(lc) + 1;

    if (rte->rtekind == RTE_RELATION)
    {
      SensitiveRelation *relation = find_relation(rte->relid, relations);
      if (relation != NULL)
        gather_relation_aids(relation, rte_index, rte, &aid_references);
    }
    else if (rte->rtekind == RTE_SUBQUERY)
    {
      QueryContext *child_context = build_context(rte->subquery, relations);
      child_contexts = lappend(child_contexts, child_context);
      gather_subquery_aids(child_context, rte_index, &aid_references);
    }
  }

  QueryContext *context = palloc(sizeof(QueryContext));
  context->query = query;
  context->child_contexts = child_contexts;
  context->aid_references = aid_references;
  return context;
}

static void append_aid_args(Aggref *aggref, QueryContext *context)
{
  bool found_any = false;

  ListCell *lc;
  foreach (lc, context->aid_references)
  {
    AidReference *aid_ref = (AidReference *)lfirst(lc);
    TargetEntry *aid_entry = make_aid_target(aid_ref, list_length(aggref->args) + 1, false);

    /* Append the AID argument to function's arguments. */
    aggref->args = lappend(aggref->args, aid_entry);
    aggref->aggargtypes = lappend_oid(aggref->aggargtypes, aid_ref->aid->atttype);

    found_any = true;
  }

  if (!found_any)
    FAILWITH("No AID found in target relations.");
}
