#ifndef QCS_QASM_DECL
#define QCS_QASM_DECL

#include <qcs/circuit.h>
#include <stdint.h>

#define QCS_QASM_REGISTER_NAME_BUFFER_LENGTH 32

typedef struct {
  char name[QCS_QASM_REGISTER_NAME_BUFFER_LENGTH];
  // registry of [offset .. offset + size - 1] in the global range
  uint32_t offset, size;
} QCS_QasmRegister;

typedef struct {
  // global indices
  uint32_t qubit, clbit;
} QCS_QasmMeasure;

// Parsed quasm program
typedef struct {
  QCS_Circuit circuit;
  QCS_QasmRegister *qregs;
  QCS_QasmRegister *cregs;
  QCS_QasmMeasure *measures;
  uint32_t clbit_count;
} QCS_QasmProgram;

#define QCS_QASM_ERROR_BUFFER_LENGTH 128

// Quasm parsing error
typedef struct {
  uint32_t line, column;
  char message[QCS_QASM_ERROR_BUFFER_LENGTH];
} QCS_QasmError;

QCS_Error qcs_qasm_parse(const char *source, QCS_QasmProgram *out,
                         QCS_QasmError *error);
void qcs_qasm_program_destroy(QCS_QasmProgram *program);

#endif
