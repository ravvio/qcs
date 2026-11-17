// OpenQASM program: a syntax tree turned into a circuit
#ifndef QCS_QASM_PROGRAM_DECL
#define QCS_QASM_PROGRAM_DECL

#include "qcs_qasm_ast.h"
#include <qcs/qasm.h>

// the program of statements into out, with the values of its inputs; on
// error out is left empty
bool qcs_qasm_program_create(const QCS_QasmStmt *statements,
                             const QCS_QasmInput *inputs, size_t input_count,
                             QCS_QasmProgram *out, QCS_QasmError *error);

#endif
