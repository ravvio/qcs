#ifndef QCS_PROGRAM_DECL
#define QCS_PROGRAM_DECL

#include "qcs/circuit.h"
#include "qcs/random.h"
#include <stddef.h>
#include <stdint.h>

// generated program operation types
typedef enum : uint8_t {
  QCS_OP_GATES,   // run a flat unitary circuit
  QCS_OP_MEASURE, // collapse qubit, write the result into clbit
  QCS_OP_RESET,   // measure, then x if 1
  QCS_OP_IF,      // run then_ops or else_ops by the clbits
} QCS_OpKind;

typedef struct QCS_Op QCS_Op;
struct QCS_Op {
  QCS_OpKind kind;
  uint32_t line, column; // source position
  union {
    // GATES with global phase
    QCS_Circuit gates;
    // MEASURE
    struct {
      uint32_t qubit, clbit;
    } measure;
    // RESET
    uint32_t qubit;
    // IF
    struct {
      uint64_t mask, value;
      QCS_Op *then_ops, *else_ops;
    } branch;
  };
};

typedef struct {
  uint32_t qubit_count, clbit_count;
  QCS_Op *ops; // stb_ds
} QCS_Program;

QCS_Program qcs_program_create(uint32_t qubit_count, uint32_t clbit_count);
size_t qcs_ops_count(const QCS_Op *ops);
void qcs_ops_destroy(QCS_Op **ops);
void qcs_program_destroy(QCS_Program *program);
void qcs_program_fdump(const QCS_Program *program, FILE *file);
void qcs_ops_append_circuit(QCS_Op **ops, const QCS_Circuit *circuit);
void qcs_program_append_circuit(QCS_Program *program,
                                const QCS_Circuit *circuit);
void qcs_ops_append_measure(QCS_Op **ops, uint32_t qubit, uint32_t clbit);
QCS_Error qcs_program_append_measure(QCS_Program *program, uint32_t qubit,
                                     uint32_t clbit);
void qcs_ops_append_reset(QCS_Op **ops, uint32_t qubit);
QCS_Error qcs_program_append_reset(QCS_Program *program, uint32_t qubit);
void qcs_ops_append_if(QCS_Op **ops, uint64_t mask, uint64_t value,
                       QCS_Op **then_ops, QCS_Op **else_ops);
QCS_Error qcs_program_append_if(QCS_Program *program, uint64_t mask,
                                uint64_t value, QCS_Op **then_ops,
                                QCS_Op **else_ops);

bool qcs_program_can_sample(const QCS_Program *program);
uint64_t qcs_program_clbits(const QCS_Program *program, size_t pattern);
QCS_Error qcs_program_run(const QCS_Program *program, size_t shots,
                          QCS_Random *random, uint64_t *clbits);

#endif
