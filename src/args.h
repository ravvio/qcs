// Arguments of the qcs CLI
#ifndef QCS_ARGS_DECL
#define QCS_ARGS_DECL

#include <qcs/qasm.h>
#include <stddef.h>
#include <stdint.h>

// Subcommand of qcs
typedef enum : uint8_t {
  QCS_SUBCOMMAND_RUN,  // run the program, print the counts
  QCS_SUBCOMMAND_DUMP, // print the program, don't run it
} QCS_Subcommand;

// Options of qcs run and qcs dump
typedef struct {
  QCS_Subcommand subcommand;
  const char *source_path; // .qasm file
  bool gpu;                // run on the GPU, else on the CPU
  size_t shots;
  uint64_t seed;
  bool state;            // print the amplitudes
  QCS_QasmInput *inputs; // names point into argv
  size_t input_count;
} QCS_Args;

// parse qcs run|dump file [flags] into out, false on a usage error; free
// out->inputs either way
bool args_parse(int argc, char **argv, QCS_Args *out);

#endif
