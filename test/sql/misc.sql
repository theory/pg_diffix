LOAD 'pg_diffix';

SET pg_diffix.low_count_min_threshold = 2;
SET pg_diffix.low_count_layer_sd = 0;

SET ROLE diffix_test;
SET pg_diffix.session_access_level = 'publish_trusted';

----------------------------------------------------------------
-- Post processing anonymized results
----------------------------------------------------------------

SELECT * FROM
  (SELECT count(*) AS num_purchases FROM test_purchases) x,
  (SELECT count(*) AS num_customers FROM test_customers) y;

SELECT
  coalesce(patients.city, customers.city) AS city,
  customers.count AS num_customers,
  patients.count AS num_patients
FROM
  (SELECT city, count(*) FROM test_patients GROUP BY 1) patients
FULL OUTER JOIN
  (SELECT city, count(*) FROM test_customers GROUP BY 1) customers
ON patients.city = customers.city;

----------------------------------------------------------------
-- Miscellaneous queries
----------------------------------------------------------------

DO $$
BEGIN
  PERFORM count(*) FROM test_customers;
END;
$$;
