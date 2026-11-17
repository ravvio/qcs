#ifndef QCS_QASM_DECL
#define QCS_QASM_DECL

#include <qcs/program.h>
#include <stdint.h>

#define QCS_QASM_REGISTER_NAME_BUFFER_LENGTH 32

typedef struct {
  char name[QCS_QASM_REGISTER_NAME_BUFFER_LENGTH];
  // registry of [offset .. offset + size - 1] in the global range
  uint32_t offset, size;
} QCS_QasmRegister;

// Parsed quasm program: its ops, and the registers that name its bits
typedef struct {
  QCS_Program program;
  QCS_QasmRegister *qregs;
  QCS_QasmRegister *cregs;
} QCS_QasmProgram;

#define QCS_QASM_ERROR_BUFFER_LENGTH 128

// Quasm parsing error
typedef struct {
  uint32_t line, column;
  char message[QCS_QASM_ERROR_BUFFER_LENGTH];
} QCS_QasmError;

// Value for an input the program declares
typedef struct {
  const char *name;
  double value;
} QCS_QasmInput;

// parse OpenQASM 3 source into out, with the values of its inputs; on error
// out is left empty and error says where
QCS_Error qcs_qasm_parse(const char *source, const QCS_QasmInput *inputs,
                         size_t input_count, QCS_QasmProgram *out,
                         QCS_QasmError *error);

// Print

void qcs_qasm_program_fdump(const QCS_QasmProgram *program, FILE *file);

// frees the ops and every array of program, leaves it empty
void qcs_qasm_program_destroy(QCS_QasmProgram *program);

#endif
