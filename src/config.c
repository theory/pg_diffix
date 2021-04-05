#include "postgres.h"
#include "lib/stringinfo.h"
#include "fmgr.h"
#include "utils/guc.h"

#include "pg_diffix/config.h"
#include "pg_diffix/utils.h"

DiffixConfig g_config = {
    .default_access_level = ACCESS_DIRECT,

    .noise_seed = "diffix",
    .noise_sigma = 1.0,
    .noise_cutoff = 5.0,

    .minimum_allowed_aids = 2,

    .outlier_count_min = 1,
    .outlier_count_max = 2,

    .top_count_min = 4,
    .top_count_max = 6,
};

static const int MAX_NUMERIC_CONFIG = 1000;

static const struct config_enum_entry default_access_level_options[] = {
    {"direct", ACCESS_DIRECT, false},
    {"publish", ACCESS_PUBLISH, false},
    {NULL, 0, false},
};

static char *config_to_string(DiffixConfig *config)
{
  StringInfoData string;
  initStringInfo(&string);

  /* begin config */
  appendStringInfo(&string, "{DIFFIX_CONFIG");

  appendStringInfo(&string, " :default_access_level %i", config->default_access_level);
  appendStringInfo(&string, " :noise_seed \"%s\"", config->noise_seed);
  appendStringInfo(&string, " :noise_sigma %f", config->noise_sigma);
  appendStringInfo(&string, " :noise_cutoff %f", config->noise_cutoff);
  appendStringInfo(&string, " :minimum_allowed_aids %i", config->minimum_allowed_aids);
  appendStringInfo(&string, " :outlier_count_min %i", config->outlier_count_min);
  appendStringInfo(&string, " :outlier_count_max %i", config->outlier_count_max);
  appendStringInfo(&string, " :top_count_min %i", config->top_count_min);
  appendStringInfo(&string, " :top_count_max %i", config->top_count_max);

  appendStringInfo(&string, "}");
  /* end config */

  return string.data;
}

void config_init(void)
{
  DefineCustomEnumVariable(
      "pg_diffix.default_access_level",                /* name */
      "Access level for users without special roles.", /* short_desc */
      NULL,                                            /* long_desc */
      &g_config.default_access_level,                  /* valueAddr */
      g_config.default_access_level,                   /* bootValue */
      default_access_level_options,                    /* options */
      PGC_SUSET,                                       /* context */
      0,                                               /* flags */
      NULL,                                            /* check_hook */
      NULL,                                            /* assign_hook */
      NULL);                                           /* show_hook */

  DefineCustomStringVariable(
      "pg_diffix.noise_seed",                     /* name */
      "Seed used for initializing noise layers.", /* short_desc */
      NULL,                                       /* long_desc */
      &g_config.noise_seed,                       /* valueAddr */
      g_config.noise_seed,                        /* bootValue */
      PGC_SUSET,                                  /* context */
      0,                                          /* flags */
      NULL,                                       /* check_hook */
      NULL,                                       /* assign_hook */
      NULL);                                      /* show_hook */

  DefineCustomRealVariable(
      "pg_diffix.noise_sigma",                            /* name */
      "Standard deviation of noise added to aggregates.", /* short_desc */
      NULL,                                               /* long_desc */
      &g_config.noise_sigma,                              /* valueAddr */
      g_config.noise_sigma,                               /* bootValue */
      0,                                                  /* minValue */
      MAX_NUMERIC_CONFIG,                                 /* maxValue */
      PGC_SUSET,                                          /* context */
      0,                                                  /* flags */
      NULL,                                               /* check_hook */
      NULL,                                               /* assign_hook */
      NULL);                                              /* show_hook */

  DefineCustomRealVariable(
      "pg_diffix.noise_cutoff",        /* name */
      "Maximum absolute noise value.", /* short_desc */
      NULL,                            /* long_desc */
      &g_config.noise_cutoff,          /* valueAddr */
      g_config.noise_cutoff,           /* bootValue */
      0,                               /* minValue */
      1e7,                             /* maxValue */
      PGC_SUSET,                       /* context */
      0,                               /* flags */
      NULL,                            /* check_hook */
      NULL,                            /* assign_hook */
      NULL);                           /* show_hook */

  DefineCustomIntVariable(
      "pg_diffix.minimum_allowed_aids",                                        /* name */
      "The minimum number of distinct AIDs that can be in a reported bucket.", /* short_desc */
      NULL,                                                                    /* long_desc */
      &g_config.minimum_allowed_aids,                                          /* valueAddr */
      g_config.minimum_allowed_aids,                                           /* bootValue */
      2,                                                                       /* minValue */
      MAX_NUMERIC_CONFIG,                                                      /* maxValue */
      PGC_SUSET,                                                               /* context */
      0,                                                                       /* flags */
      NULL,                                                                    /* check_hook */
      NULL,                                                                    /* assign_hook */
      NULL);                                                                   /* show_hook */

  DefineCustomIntVariable(
      "pg_diffix.outlier_count_min",        /* name */
      "Minimum outlier count (inclusive).", /* short_desc */
      NULL,                                 /* long_desc */
      &g_config.outlier_count_min,          /* valueAddr */
      g_config.outlier_count_min,           /* bootValue */
      0,                                    /* minValue */
      MAX_NUMERIC_CONFIG,                   /* maxValue */
      PGC_SUSET,                            /* context */
      0,                                    /* flags */
      NULL,                                 /* check_hook */
      NULL,                                 /* assign_hook */
      NULL);                                /* show_hook */

  DefineCustomIntVariable(
      "pg_diffix.outlier_count_max",        /* name */
      "Maximum outlier count (inclusive).", /* short_desc */
      NULL,                                 /* long_desc */
      &g_config.outlier_count_max,          /* valueAddr */
      g_config.outlier_count_max,           /* bootValue */
      0,                                    /* minValue */
      MAX_NUMERIC_CONFIG,                   /* maxValue */
      PGC_SUSET,                            /* context */
      0,                                    /* flags */
      NULL,                                 /* check_hook */
      NULL,                                 /* assign_hook */
      NULL);                                /* show_hook */

  DefineCustomIntVariable(
      "pg_diffix.top_count_min",                     /* name */
      "Minimum top contributors count (inclusive).", /* short_desc */
      NULL,                                          /* long_desc */
      &g_config.top_count_min,                       /* valueAddr */
      g_config.top_count_min,                        /* bootValue */
      1,                                             /* minValue */
      MAX_NUMERIC_CONFIG,                            /* maxValue */
      PGC_SUSET,                                     /* context */
      0,                                             /* flags */
      NULL,                                          /* check_hook */
      NULL,                                          /* assign_hook */
      NULL);                                         /* show_hook */

  DefineCustomIntVariable(
      "pg_diffix.top_count_max",                     /* name */
      "Maximum top contributors count (inclusive).", /* short_desc */
      NULL,                                          /* long_desc */
      &g_config.top_count_max,                       /* valueAddr */
      g_config.top_count_max,                        /* bootValue */
      1,                                             /* minValue */
      MAX_NUMERIC_CONFIG,                            /* maxValue */
      PGC_SUSET,                                     /* context */
      0,                                             /* flags */
      NULL,                                          /* check_hook */
      NULL,                                          /* assign_hook */
      NULL);                                         /* show_hook */

  char *config_str = config_to_string(&g_config);
  DEBUG_LOG("Config %s", config_str);
  pfree(config_str);
}
