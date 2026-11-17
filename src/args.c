#include "args.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// convert the whole text to an unsigned integer, digits only
static bool text_to_u64(const char *text, uint64_t *out) {
  if (text[0] < '0' || text[0] > '9') {
    return false;
  }
  char *end;
  errno = 0;
  unsigned long long value = strtoull(text, &end, 10);
  if (*end != '\0' || errno == ERANGE) {
    return false;
  }
  *out = value;
  return true;
}

// convert the whole text to a double
static bool text_to_double(const char *text, double *out) {
  char *end;
  errno = 0;
  double value = strtod(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE) {
    return false;
  }
  *out = value;
  return true;
}

bool args_parse(int argc, char **argv, QCS_Args *out) {
  // set the defaults
  *out = (QCS_Args){.gpu = true, .shots = 1024, .seed = 1};

  // check the subcommand and the file
  if (argc < 3) {
    return false;
  }
  if (strcmp(argv[1], "run") == 0) {
    out->subcommand = QCS_SUBCOMMAND_RUN;
  } else if (strcmp(argv[1], "dump") == 0) {
    out->subcommand = QCS_SUBCOMMAND_DUMP;
  } else {
    return false;
  }
  out->source_path = argv[2];
  out->inputs = malloc((size_t)argc * sizeof *out->inputs);
  if (!out->inputs) {
    return false;
  }

  // read the flags: --state alone, the others with a value
  for (int i = 3; i < argc; i++) {
    const char *flag = argv[i];

    if (strcmp(flag, "--state") == 0) {
      out->state = true;
      continue;
    }

    if (i + 1 >= argc) {
      return false;
    }
    char *value = argv[++i];

    if (strcmp(flag, "--mode") == 0) {
      if (strcmp(value, "cpu") == 0) {
        out->gpu = false;
      } else if (strcmp(value, "gpu") == 0) {
        out->gpu = true;
      } else {
        return false;
      }
    } else if (strcmp(flag, "--shots") == 0) {
      uint64_t shots;
      if (!text_to_u64(value, &shots) || shots == 0) {
        return false;
      }
      out->shots = (size_t)shots;
    } else if (strcmp(flag, "--seed") == 0) {
      if (!text_to_u64(value, &out->seed)) {
        return false;
      }
    } else if (strcmp(flag, "--input") == 0) {
      // split name=value at the first '='
      char *equals = strchr(value, '=');
      if (!equals || equals == value) {
        return false;
      }
      *equals = '\0';
      QCS_QasmInput input = {.name = value};
      if (!text_to_double(equals + 1, &input.value)) {
        return false;
      }
      out->inputs[out->input_count++] = input;
    } else {
      return false;
    }
  }
  return true;
}
