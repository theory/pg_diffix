#ifndef PG_DIFFIX_VALIDATION_H
#define PG_DIFFIX_VALIDATION_H

#include "nodes/parsenodes.h"

/*
 * Verifies that the command is allowed. SUPERUSER can issue any command, for other users it depends on their
 * `pg_diffix.access_level` setting.
 *
 * If requirements are not met, an error is reported and execution is halted.
 */
extern void verify_command(Query *query);

/*
 * Verifies that a query matches current anonymization restrictions and limitations.
 * If requirements are not met, an error is reported and execution is halted.
 *
 * Some part of verification is up to `verify_anonymizing_query`.
 */
extern void verify_anonymization_requirements(Query *query);

/*
 * Similar to `verify_anonymization_requirements`, operates on an anonymizing query.
 */
extern void verify_anonymizing_query(Query *query);

/*
 * Returns `true` if the given type represents a supported numeric type.
 */
extern bool is_supported_numeric_type(Oid type);

/*
 * Returns the numeric value as a `double`.
 */
extern double numeric_value_to_double(Oid type, Datum value);

#endif /* PG_DIFFIX_VALIDATION_H */
