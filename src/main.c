#include "args.h"

#include <errno.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// print how to call qcs
static void usage_print(void) {
  fputs("usage: qcs run file.qasm [--mode cpu|gpu] [--shots N] [--seed S]\n"
        "                         [--state] [--input name=value]...\n"
        "       qcs dump file.qasm [--input name=value]...\n"
        "       qcs                prints the GPU's qubit limit\n",
        stderr);
}

// read the whole file at path into a NUL-terminated buffer, NULL on error
static char *source_read(const char *path) {
  // open it
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "[e] %s: could not open: %s\n", path, strerror(errno));
    return NULL;
  }

  // get its size
  if (fseek(file, 0, SEEK_END) != 0) {
    fprintf(stderr, "[e] %s: could not seek: %s\n", path, strerror(errno));
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fprintf(stderr, "[e] %s: could not get the size: %s\n", path,
            strerror(errno));
    fclose(file);
    return NULL;
  }
  rewind(file);

  // read it, add the NUL
  size_t filesize = (size_t)size;
  char *source = malloc(filesize + 1);
  if (source == NULL) {
    fprintf(stderr, "[e] %s: out of memory for %zu bytes\n", path, filesize);
    fclose(file);
    return NULL;
  }
  if (fread(source, 1, filesize, file) != filesize) {
    fprintf(stderr, "[e] %s: %s\n", path,
            ferror(file) ? "could not read" : "shorter than its size");
    fclose(file);
    free(source);
    return NULL;
  }
  source[filesize] = '\0';
  fclose(file);
  return source;
}

// run program on the GPU, each shot's clbits into clbits
static bool gpu_program_run(const QCS_Program *program, size_t shots,
                            QCS_Random *random, uint64_t *clbits) {
  // start the GPU with room for the state
  if (qcs_gpu_init(program->qubit_count) != QCS_ERR_OK) {
    return false;
  }
  bool ok = qcs_gpu_program_run(program, shots, random, clbits) == QCS_ERR_OK;
  qcs_gpu_terminate();
  return ok;
}

// compare two keys for qsort
static int key_compare(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a;
  uint64_t y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

// print how many shots gave each clbits value, width bits each; sorts clbits
static void counts_print(uint64_t *clbits, size_t shots, uint32_t width) {
  // sort them, in bitstring order
  qsort(clbits, shots, sizeof *clbits, key_compare);

  // print each run of equal values: its bits, then its length
  for (size_t i = 0; i < shots;) {
    size_t j = i;
    while (j < shots && clbits[j] == clbits[i]) {
      j++;
    }
    for (uint32_t b = width; b > 0; b--) {
      putchar((clbits[i] >> (b - 1)) & 1 ? '1' : '0');
    }
    printf(": %zu\n", j - i);
    i = j;
  }
}

// print the GPU's qubit limit
static bool gpu_limit_print(void) {
  if (qcs_gpu_init(0) != QCS_ERR_OK) {
    return false;
  }
  fprintf(stderr, "[i] GPU: up to %u qubits\n", qcs_gpu_max_qubits());
  qcs_gpu_terminate();
  return true;
}

int main(int argc, char **argv) {
  // no arguments: print the GPU's qubit limit
  if (argc < 2) {
    return gpu_limit_print() ? 0 : 1;
  }

  // parse the arguments
  QCS_Args args;
  if (!args_parse(argc, argv, &args)) {
    usage_print();
    free(args.inputs);
    return 2;
  }

  // load the file
  char *source = source_read(args.source_path);
  if (source == NULL) {
    free(args.inputs);
    return 1;
  }

  // parse the source, then free it and the inputs
  QCS_QasmProgram program;
  QCS_QasmError error;
  QCS_Error parsed =
      qcs_qasm_parse(source, args.inputs, args.input_count, &program, &error);
  free(source);
  free(args.inputs);
  if (parsed != QCS_ERR_OK) {
    if (error.line == 0) {
      fprintf(stderr, "[e] %s: %s\n", args.source_path, error.message);
    } else {
      fprintf(stderr, "[e] %s:%u:%u: %s\n", args.source_path, error.line,
              error.column, error.message);
    }
    return 1;
  }

  // dump: print the program, don't run it
  if (args.subcommand == QCS_SUBCOMMAND_DUMP) {
    qcs_qasm_program_fdump(&program, stdout);
    qcs_qasm_program_destroy(&program);
    return 0;
  }
  int status = 1;

  // no bits: measure every qubit into its own clbit
  QCS_Program *run = &program.program;
  if (run->clbit_count == 0) {
    for (uint32_t q = 0; q < run->qubit_count; q++) {
      qcs_program_append_measure(run, q, q);
    }
  }

  // run the shots
  QCS_Random random = qcs_random_create(args.seed);
  uint64_t *clbits = calloc(args.shots, sizeof *clbits);
  if (clbits == NULL) {
    fprintf(stderr, "[e] out of memory for %zu shots\n", args.shots);
    goto destroy_program;
  }
  bool ok = args.gpu ? gpu_program_run(run, args.shots, &random, clbits)
                     : qcs_program_run(run, args.shots, &random, clbits) ==
                           QCS_ERR_OK;
  if (!ok) {
    goto free_clbits;
  }

  // count
  counts_print(clbits, args.shots, run->clbit_count);
  status = 0;

free_clbits:
  free(clbits);
destroy_program:
  qcs_qasm_program_destroy(&program);
  return status;
}
