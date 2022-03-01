LOAD 'pg_diffix';

SET pg_diffix.noise_layer_sd = 7;
SET pg_diffix.low_count_layer_sd = 3;

SET pg_diffix.session_access_level = 'publish_trusted';

----------------------------------------------------------------
-- Sanity checks
----------------------------------------------------------------

SELECT diffix.access_level();

----------------------------------------------------------------
-- Basic queries
----------------------------------------------------------------

SELECT COUNT(*) FROM test_customers;
SELECT COUNT(*) FROM test_purchases;

SELECT COUNT(city), COUNT(DISTINCT city) FROM test_customers;

SELECT COUNT(DISTINCT cid) FROM test_purchases;

SELECT city, COUNT(DISTINCT id) FROM test_customers GROUP BY 1;

----------------------------------------------------------------
-- Basic queries - expanding constants in target expressions
----------------------------------------------------------------

SELECT 1 FROM test_patients;
SELECT cast(1 as real) FROM test_patients;
SELECT 1, COUNT(*) FROM test_patients;
SELECT 1, city FROM test_customers;
SELECT city, 'aaaa' FROM test_customers;

----------------------------------------------------------------
-- Multi-AID queries
----------------------------------------------------------------

SELECT city FROM test_patients GROUP BY 1;

SELECT COUNT(*), COUNT(city), COUNT(DISTINCT city) FROM test_patients;

----------------------------------------------------------------
-- LCF & Filtering
----------------------------------------------------------------

SELECT id FROM test_customers;

SELECT city FROM test_customers;

SELECT city FROM test_customers GROUP BY 1 HAVING length(city) <> 4;

SELECT COUNT(*), COUNT(city), COUNT(DISTINCT city) FROM london_customers;

----------------------------------------------------------------
-- Empty tables
----------------------------------------------------------------

SELECT COUNT(*), COUNT(city), COUNT(DISTINCT city) FROM empty_test_customers;
