#include "qcs/program.h"
#include "qcs/state.h"
#include <qcs/qcs.h>
#include <stb_ds.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QCS_Program qcs_program_create(uint32_t qubit_count, uint32_t clbit_count) {
  return (QCS_Program){
      .qubit_count = qubit_count,
      .clbit_count = clbit_count,
  };
}

size_t qcs_ops_count(const QCS_Op *ops) { return arrlenu(ops); }

// append a GATES op to an operation list
void qcs_ops_append_circuit(QCS_Op **ops, const QCS_Circuit *circuit) {
  // a new run, unless ops ends with one
  if (arrlen(*ops) == 0 || arrlast(*ops).kind != QCS_OP_GATES) {
    arrput(*ops, ((QCS_Op){.kind = QCS_OP_GATES}));
  }

  // set qubits, gates and phase
  QCS_Circuit *run = &arrlast(*ops).gates;
  if (run->qubit_count < circuit->qubit_count) {
    run->qubit_count = circuit->qubit_count;
  }
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    arrput(run->gates, circuit->gates[g]);
  }
  run->global_phase += circuit->global_phase;
}

// append (or merge) a GATES to the program
void qcs_program_append_circuit(QCS_Program *program,
                                const QCS_Circuit *circuit) {
  if (program->qubit_count < circuit->qubit_count) {
    program->qubit_count = circuit->qubit_count;
  }
  qcs_ops_append_circuit(&program->ops, circuit);
}

// append a MEASURE op to the operation list
void qcs_ops_append_measure(QCS_Op **ops, uint32_t qubit, uint32_t clbit) {
  QCS_Op op = {
      .kind = QCS_OP_MEASURE,
      .measure = {.qubit = qubit, .clbit = clbit},
  };
  arrput(*ops, op);
}

// append a MEASURE op to the program
QCS_Error qcs_program_append_measure(QCS_Program *program, uint32_t qubit,
                                     uint32_t clbit) {
  if (qubit >= QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] invalid qubit: %u, at most %u qubits\n", qubit,
            QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }
  if (clbit >= QCS_MAX_CLBITS) {
    fprintf(stderr, "[e] invalid clbit: %u, at most %u clbits\n", clbit,
            QCS_MAX_CLBITS);
    return QCS_ERR_MAX_BITS;
  }
  if (program->qubit_count <= qubit) {
    program->qubit_count = qubit + 1;
  }
  if (program->clbit_count <= clbit) {
    program->clbit_count = clbit + 1;
  }
  qcs_ops_append_measure(&program->ops, qubit, clbit);
  return QCS_ERR_OK;
}

// append a RESET op to the operation list
void qcs_ops_append_reset(QCS_Op **ops, uint32_t qubit) {
  QCS_Op op = {
      .kind = QCS_OP_RESET,
      .qubit = qubit,
  };
  arrput(*ops, op);
}

// append a RESET op to the program
QCS_Error qcs_program_append_reset(QCS_Program *program, uint32_t qubit) {
  if (qubit >= QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] invalid qubit: %u, at most %u qubits\n", qubit,
            QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }
  if (program->qubit_count <= qubit) {
    program->qubit_count = qubit + 1;
  }
  qcs_ops_append_reset(&program->ops, qubit);
  return QCS_ERR_OK;
}

// free the operation list, its runs and branches, and set it to NULL
void qcs_ops_destroy(QCS_Op **ops) {
  for (size_t i = 0; i < qcs_ops_count(*ops); i++) {
    QCS_Op *op = &(*ops)[i];
    if (op->kind == QCS_OP_GATES) {
      qcs_circuit_destroy(&op->gates);
    } else if (op->kind == QCS_OP_IF) {
      qcs_ops_destroy(&op->branch.then_ops);
      qcs_ops_destroy(&op->branch.else_ops);
    }
  }
  arrfree(*ops);
}

// free the program and empty it
void qcs_program_destroy(QCS_Program *program) {
  qcs_ops_destroy(&program->ops);
  *program = (QCS_Program){0};
}

// write ops into file, one per line, two spaces per depth
static void qcs_ops_fdump(const QCS_Op *ops, int depth, FILE *file) {
  int indent = 2 * depth;
  for (size_t i = 0; i < qcs_ops_count(ops); i++) {
    const QCS_Op *op = &ops[i];
    switch (op->kind) {
    case QCS_OP_GATES:
      if (op->gates.global_phase != 0) {
        fprintf(file, "%*sgphase(%.6g)\n", indent, "", op->gates.global_phase);
      }
      for (size_t g = 0; g < qcs_circuit_gate_count(&op->gates); g++) {
        fprintf(file, "%*s", indent, "");
        qcs_gate_fdump(op->gates.gates[g], file);
        fputc('\n', file);
      }
      break;
    case QCS_OP_MEASURE:
      fprintf(file, "%*smeasure q[%u] -> c[%u]\n", indent, "",
              op->measure.qubit, op->measure.clbit);
      break;
    case QCS_OP_RESET:
      fprintf(file, "%*sreset q[%u]\n", indent, "", op->qubit);
      break;
    case QCS_OP_IF:
      // condition, one clbit per mask bit, lowest first
      fprintf(file, "%*sif", indent, "");
      if (op->branch.mask == 0) {
        fputs(" true", file);
      }
      for (uint32_t c = 0; c < 64; c++) {
        if (op->branch.mask >> c & 1) {
          bool first = (op->branch.mask & ((1ull << c) - 1)) == 0;
          fprintf(file, "%s c[%u] == %u", first ? "" : " &&", c,
                  (uint32_t)(op->branch.value >> c & 1));
        }
      }

      // then, else if not empty
      fputs(" {\n", file);
      qcs_ops_fdump(op->branch.then_ops, depth + 1, file);
      if (qcs_ops_count(op->branch.else_ops) > 0) {
        fprintf(file, "%*s} else {\n", indent, "");
        qcs_ops_fdump(op->branch.else_ops, depth + 1, file);
      }
      fprintf(file, "%*s}\n", indent, "");
      break;
    }
  }
}

// write the program's counts, then its ops
void qcs_program_fdump(const QCS_Program *program, FILE *file) {
  fprintf(file, "qubits %u, clbits %u\n", program->qubit_count,
          program->clbit_count);
  qcs_ops_fdump(program->ops, 0, file);
}

// append an IF op to the operation list, moving then_ops and else_ops
void qcs_ops_append_if(QCS_Op **ops, uint64_t mask, uint64_t value,
                       QCS_Op **then_ops, QCS_Op **else_ops) {
  QCS_Op op = {
      .kind = QCS_OP_IF,
      .branch = {.mask = mask, .value = value, .then_ops = *then_ops},
  };
  *then_ops = NULL;
  if (else_ops) {
    op.branch.else_ops = *else_ops;
    *else_ops = NULL;
  }
  arrput(*ops, op);
}

// grow qubit_count and clbit_count to cover op, branches included
static void qcs_op_grow(const QCS_Op *op, uint32_t *qubit_count,
                        uint32_t *clbit_count) {
  uint32_t qubits = 0, clbits = 0;
  switch (op->kind) {
  case QCS_OP_GATES:
    qubits = op->gates.qubit_count;
    break;
  case QCS_OP_MEASURE:
    qubits = op->measure.qubit + 1;
    clbits = op->measure.clbit + 1;
    break;
  case QCS_OP_RESET:
    qubits = op->qubit + 1;
    break;
  case QCS_OP_IF:
    // clbits up to the mask's highest bit
    while (clbits < 64 && op->branch.mask >> clbits) {
      clbits++;
    }
    for (size_t i = 0; i < qcs_ops_count(op->branch.then_ops); i++) {
      qcs_op_grow(&op->branch.then_ops[i], qubit_count, clbit_count);
    }
    for (size_t i = 0; i < qcs_ops_count(op->branch.else_ops); i++) {
      qcs_op_grow(&op->branch.else_ops[i], qubit_count, clbit_count);
    }
    break;
  }
  if (*qubit_count < qubits) {
    *qubit_count = qubits;
  }
  if (*clbit_count < clbits) {
    *clbit_count = clbits;
  }
}

// append an IF op to the program, moving then_ops and else_ops
QCS_Error qcs_program_append_if(QCS_Program *program, uint64_t mask,
                                uint64_t value, QCS_Op **then_ops,
                                QCS_Op **else_ops) {
  if (value & ~mask) {
    fprintf(stderr, "[e] invalid if: value 0x%llx outside mask 0x%llx\n",
            (unsigned long long)value, (unsigned long long)mask);
    return QCS_ERR_MAX_BITS;
  }

  // count the bits the if uses
  const QCS_Op op = {
      .kind = QCS_OP_IF,
      .branch = {.mask = mask,
                 .then_ops = *then_ops,
                 .else_ops = else_ops ? *else_ops : NULL},
  };
  uint32_t qubit_count = program->qubit_count;
  uint32_t clbit_count = program->clbit_count;
  qcs_op_grow(&op, &qubit_count, &clbit_count);
  if (qubit_count > QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] invalid if: uses %u qubits, at most %u\n", qubit_count,
            QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }
  if (clbit_count > QCS_MAX_CLBITS) {
    fprintf(stderr, "[e] invalid if: uses %u clbits, at most %u\n", clbit_count,
            QCS_MAX_CLBITS);
    return QCS_ERR_MAX_BITS;
  }

  // grow the program and append
  program->qubit_count = qubit_count;
  program->clbit_count = clbit_count;
  qcs_ops_append_if(&program->ops, mask, value, then_ops, else_ops);
  return QCS_ERR_OK;
}

bool qcs_program_can_sample(const QCS_Program *program) {
  uint64_t measured = 0;
  for (size_t i = 0; i < qcs_ops_count(program->ops); i++) {
    QCS_Op *op = &program->ops[i];
    switch (op->kind) {
    case QCS_OP_MEASURE:
      measured |= 1ull << op->measure.qubit;
      break;
    case QCS_OP_RESET:
    case QCS_OP_IF:
      return false;
    case QCS_OP_GATES:
      for (size_t g = 0; g < qcs_circuit_gate_count(&op->gates); g++) {
        QCS_Gate gate = op->gates.gates[g];
        if ((measured & (1ull << gate.target))
            // not diagonal (diagonal gates only act on phase)
            && (gate.m[0][1] != 0 || gate.m[1][0] != 0)) {
          return false;
        }
      }
      break;
    }
  }
  return true;
}

// apply op to state, writing measured bits into clbits
static QCS_Error qcs_op_run(const QCS_Op *op, QCS_State *state,
                            uint64_t *clbits, QCS_Random *random) {
  switch (op->kind) {
  case QCS_OP_GATES:
    return qcs_circuit_run(&op->gates, state);
  case QCS_OP_MEASURE: {
    bool value;
    QCS_Error err = qcs_state_measure(state, op->measure.qubit, random, &value);
    if (err != QCS_ERR_OK) {
      return err;
    }
    uint64_t bit = 1ull << op->measure.clbit;
    *clbits = value ? *clbits | bit : *clbits & ~bit;
    return QCS_ERR_OK;
  }
  case QCS_OP_RESET:
    return qcs_state_reset(state, op->qubit, random);
  case QCS_OP_IF: {
    // the branch the clbits pick
    bool taken = (*clbits & op->branch.mask) == op->branch.value;
    const QCS_Op *ops = taken ? op->branch.then_ops : op->branch.else_ops;
    for (size_t i = 0; i < qcs_ops_count(ops); i++) {
      QCS_Error err = qcs_op_run(&ops[i], state, clbits, random);
      if (err != QCS_ERR_OK) {
        return err;
      }
    }
    return QCS_ERR_OK;
  }
  }
  return QCS_ERR_OK;
}

// clbits of a sampled pattern: each measure copies its qubit's bit into its
// clbit, in program order
uint64_t qcs_program_clbits(const QCS_Program *program, size_t pattern) {
  uint64_t bits = 0;
  for (size_t i = 0; i < qcs_ops_count(program->ops); i++) {
    const QCS_Op *op = &program->ops[i];
    if (op->kind == QCS_OP_MEASURE) {
      uint64_t bit = 1ull << op->measure.clbit;
      bool value = pattern >> op->measure.qubit & 1;
      bits = value ? bits | bit : bits & ~bit;
    }
  }
  return bits;
}

// run the gates once, sample state, set each shot's clbits from its pattern
static QCS_Error qcs_program_run_sampled(const QCS_Program *program,
                                         size_t shots, QCS_Random *random,
                                         QCS_State *state, uint64_t *clbits) {
  // run every gate
  for (size_t i = 0; i < qcs_ops_count(program->ops); i++) {
    if (program->ops[i].kind == QCS_OP_GATES) {
      QCS_Error err = qcs_circuit_run(&program->ops[i].gates, state);
      if (err != QCS_ERR_OK) {
        return err;
      }
    }
  }

  // sample the patterns
  size_t *outcomes = malloc(shots * sizeof *outcomes);
  if (outcomes == NULL && shots > 0) {
    fprintf(stderr, "[e] out of memory for %zu shots\n", shots);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  QCS_Error err = qcs_state_sample(state, shots, random, outcomes);

  // read each shot's clbits off its pattern
  for (size_t s = 0; err == QCS_ERR_OK && s < shots; s++) {
    clbits[s] = qcs_program_clbits(program, outcomes[s]);
  }
  free(outcomes);
  return err;
}

// run the leading gates into prefix once, the rest per shot from a copy
static QCS_Error qcs_program_run_shots(const QCS_Program *program, size_t shots,
                                       QCS_Random *random, QCS_State *prefix,
                                       uint64_t *clbits) {
  // the prefix: runs merge, so at most the first op
  size_t first = 0;
  if (qcs_ops_count(program->ops) > 0 && program->ops[0].kind == QCS_OP_GATES) {
    QCS_Error err = qcs_circuit_run(&program->ops[0].gates, prefix);
    if (err != QCS_ERR_OK) {
      return err;
    }
    first = 1;
  }

  // every shot from a copy of the prefix
  QCS_State state;
  QCS_Error err = qcs_state_create(prefix->n, &state);
  if (err != QCS_ERR_OK) {
    return err;
  }
  size_t bytes = qcs_state_size(prefix) * sizeof *prefix->amps;
  for (size_t s = 0; err == QCS_ERR_OK && s < shots; s++) {
    memcpy(state.amps, prefix->amps, bytes);
    uint64_t bits = 0;
    for (size_t i = first; err == QCS_ERR_OK && i < qcs_ops_count(program->ops);
         i++) {
      err = qcs_op_run(&program->ops[i], &state, &bits, random);
    }
    clbits[s] = bits;
  }
  qcs_state_destroy(&state);
  return err;
}

// run program shots times, each shot's clbits into clbits
QCS_Error qcs_program_run(const QCS_Program *program, size_t shots,
                          QCS_Random *random, uint64_t *clbits) {
  // Sample or per-shot run
  bool sample = qcs_program_can_sample(program);

  QCS_State state;
  QCS_Error err = qcs_state_create(program->qubit_count, &state);
  if (err != QCS_ERR_OK) {
    return err;
  }
  err = sample ? qcs_program_run_sampled(program, shots, random, &state, clbits)
               : qcs_program_run_shots(program, shots, random, &state, clbits);
  qcs_state_destroy(&state);
  return err;
}
