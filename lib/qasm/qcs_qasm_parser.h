// OpenQASM 2 and 3 parser into a syntax tree
#ifndef QCS_QASM_PARSER_DECL
#define QCS_QASM_PARSER_DECL

#include "qcs_qasm_ast.h"
#include <qcs/qasm.h>

#define QCS_QASM_MAX_EXPRESSION_DEPTH 64
#define QCS_QASM_MAX_BLOCK_DEPTH 32

// Built-in function: name and its C function
typedef struct {
  const char *name;
  double (*apply)(double);
} QCS_QasmFunction;

// function named by token, or NULL
const QCS_QasmFunction *qcs_qasm_token_function(const QCS_QasmToken *token);

// source as a program, its statements in arena
bool qcs_qasm_parse_program(const char *source, QCS_Arena *arena,
                            QCS_QasmStmt **out, QCS_QasmError *error);

// source as exactly one expression, its tree in arena
bool qcs_qasm_parse_expression(const char *source, QCS_Arena *arena,
                               QCS_QasmExpr **out, QCS_QasmError *error);

#endif
