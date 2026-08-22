// Export only reference-station vertical offsets from a libtcd database.
// This is an offline authoring utility: neither libtcd nor the source TCD is
// required by, linked into, or distributed with the X-Tidal runtime.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcd.h"

static void quoted(const char *text) {
  putchar('"');
  for (; *text; ++text) {
    const unsigned char value = (unsigned char)*text;
    if (value == '"' || value == '\\') {
      putchar('\\');
      putchar(value);
    } else if (value == '\n') {
      fputs("\\n", stdout);
    } else if (value == '\r') {
      fputs("\\r", stdout);
    } else if (value == '\t') {
      fputs("\\t", stdout);
    } else if (value >= 0x20) {
      putchar(value);
    }
  }
  putchar('"');
}

int main(int argc, char **argv) {
  if (argc != 6) {
    fprintf(stderr,
            "usage: export_tcd_chart_datums INPUT.tcd SOURCE_URL RELEASE "
            "USAGE_TERMS INPUT_SHA256\n");
    return 2;
  }
  if (!open_tide_db(argv[1])) {
    fprintf(stderr, "could not open TCD database\n");
    return 1;
  }

  const DB_HEADER_PUBLIC header = get_tide_db_header();
  printf("{\"schema\":\"xtidal-chart-datum-stations\","
         "\"schema_version\":1,\"source_provenance\":{");
  printf("\"source_url\":");
  quoted(argv[2]);
  printf(",\"release\":");
  quoted(argv[3]);
  printf(",\"usage_terms\":");
  quoted(argv[4]);
  printf(",\"input_sha256\":");
  quoted(argv[5]);
  printf("},\"record_count\":%u,\"stations\":[",
         header.number_of_records);

  int first = 1;
  for (NV_U_INT32 index = 0; index < header.number_of_records; ++index) {
    TIDE_RECORD record;
    if (read_tide_record((NV_INT32)index, &record) < 0 ||
        record.header.record_type != REFERENCE_STATION ||
        !isfinite(record.datum_offset))
      continue;
    const char *units = get_level_units(record.level_units);
    if (strcmp(units, "meters") != 0 && strcmp(units, "feet") != 0)
      continue;
    double offset_m = record.datum_offset;
    if (strcmp(units, "feet") == 0) offset_m *= 0.3048;
    if (!first) putchar(',');
    first = 0;
    printf("{\"record\":%u,\"name\":", index);
    quoted(record.header.name);
    printf(",\"latitude\":%.8f,\"longitude\":%.8f,",
           record.header.latitude, record.header.longitude);
    printf("\"msl_above_chart_datum_m\":%.6f,\"source_datum\":",
           offset_m);
    quoted(get_datum(record.datum));
    printf(",\"country\":");
    quoted(get_country(record.country));
    printf(",\"source\":");
    quoted(record.source);
    printf(",\"restriction\":");
    quoted(get_restriction(record.restriction));
    printf(",\"legalese\":");
    quoted(get_legalese(record.legalese));
    printf(",\"confidence\":%u,\"months_on_station\":%u}",
           record.confidence, record.months_on_station);
  }
  puts("]}");
  close_tide_db();
  return 0;
}
