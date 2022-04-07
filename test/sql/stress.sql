LOAD 'pg_diffix';

-- Prepare data.
CREATE TABLE test_stress AS (
  SELECT
    i AS id, left(md5(random()::text), 4) AS t, (random() * 10.0)::real AS r, round(random() * 1000)::integer AS i
  FROM generate_series(1, 50000) series(i)
);

CALL diffix.make_personal('public', 'test_stress', '646966666978', 'id');

-- Prepare test session.
SET pg_diffix.noise_layer_sd = 0;
SET pg_diffix.low_count_layer_sd = 0;
SET pg_diffix.session_access_level = 'publish_trusted';

-- Sanity checks.
SELECT diffix.access_level();
SELECT COUNT(*) FROM test_stress;

-- Stress tests.
DO $$
BEGIN
  FOR _ IN 1..3 LOOP
    PERFORM COUNT(*), COUNT(t), COUNT(DISTINCT i) FROM test_stress;
    PERFORM t, COUNT(r), COUNT(i) FROM test_stress GROUP BY 1;
    PERFORM i, round(r), COUNT(*) FROM test_stress GROUP BY 1, 2;
  END LOOP;
END;
$$;
