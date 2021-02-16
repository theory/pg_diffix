#include "postgres.h"
#include "common/hashfn.h"

#include <math.h>

#include "pg_diffix/random.h"
#include "pg_diffix/config.h"

extern double pg_erand48(unsigned short xseed[3]);

static inline double clamp_noise(double noise)
{
  return fmin(fmax(noise, -Config.noise_cutoff), Config.noise_cutoff);
}

uint64 make_seed(uint64 noise_layer_seed)
{
  uint64 base_seed = hash_bytes_extended(
      (unsigned char *)Config.noise_seed,
      strlen(Config.noise_seed),
      0);

  return hash_combine64(base_seed, noise_layer_seed);
}

double next_gaussian_double(uint64 *seed, double stddev)
{
  /*
   * Marsaglia polar method
   * Source: http://c-faq.com/lib/gaussian.html
   */
  double v1, v2, s;
  do
  {
    double u1 = pg_erand48((unsigned short *)seed);
    double u2 = pg_erand48((unsigned short *)seed);

    v1 = 2 * u1 - 1;
    v2 = 2 * u2 - 1;
    s = v1 * v1 + v2 * v2;
  } while (s >= 1 || s == 0);

  /*
   * We discard one sample...
   * Needs optimization in case we need to generate multiple numbers.
   */
  return stddev * v1 * sqrt(-2 * log(s) / s);
}

int next_uniform_int(uint64 *seed, int min, int max)
{
  return min + pg_erand48((unsigned short *)seed) * (max - min);
}

int64 apply_noise(int64 value, uint64 *seed)
{
  value += round(clamp_noise(next_gaussian_double(seed, Config.noise_sigma)));
  return Max(value, 0);
}
