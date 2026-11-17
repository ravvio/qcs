#include "qcs_simulator.h"
#include <qcs/qcs.h>
#include <stb_ds.h>
#include <stdio.h>
#include <stdlib.h>

// Most if levels the executor follows, one inside another
#define QCS_SIMULATOR_MAX_DEPTH 64

// Position in the op tree: the list and the next index at each if level
typedef struct {
  struct {
    const QCS_Op *ops;
    size_t index;
  } frames[QCS_SIMULATOR_MAX_DEPTH];
  uint32_t depth;
} QCS_Cursor;

// Shots that drew the same values so far: where they are, their state and
// clbits
typedef struct {
  QCS_Cursor cursor;
  void *state;
  uint64_t clbits;
  size_t shots;
} QCS_Branch;

// find where the top-level ops can be sampled from: no if or reset after it,
// and no measured qubit disturbed after its measure
static size_t qcs_ops_sample_start(const QCS_Op *ops) {
  size_t start = qcs_ops_count(ops);
  uint32_t disturbed = 0; // qubits a later gate targets off the diagonal
  for (size_t i = qcs_ops_count(ops); i-- > 0;) {
    const QCS_Op *op = &ops[i];
    if (op->kind == QCS_OP_IF || op->kind == QCS_OP_RESET) {
      break;
    }
    if (op->kind == QCS_OP_MEASURE && (disturbed >> op->measure.qubit & 1)) {
      break;
    }
    if (op->kind == QCS_OP_GATES) {
      for (size_t g = 0; g < qcs_circuit_gate_count(&op->gates); g++) {
        QCS_Gate gate = op->gates.gates[g];
        if (gate.m[0][1] != 0 || gate.m[1][0] != 0) {
          disturbed |= 1u << gate.target;
        }
      }
    }
    start = i;
  }
  return start;
}

// set or clear bit of clbits
static uint64_t qcs_clbits_set(uint64_t clbits, uint32_t bit, bool value) {
  return value ? clbits | 1ull << bit : clbits & ~(1ull << bit);
}

// collapse the branch's qubit onto value, then flip it back to 0 for a reset
// or write it into the clbit for a measure
static QCS_Error qcs_simulator_settle(const QCS_Simulator *simulator,
                                      QCS_Branch *branch, const QCS_Op *op,
                                      uint32_t qubit, bool value,
                                      const double p[2]) {
  QCS_Error err =
      simulator->collapse(simulator->context, branch->state, qubit, value, p);
  if (err != QCS_ERR_OK) {
    return err;
  }
  if (op->kind == QCS_OP_MEASURE) {
    branch->clbits = qcs_clbits_set(branch->clbits, op->measure.clbit, value);
    return QCS_ERR_OK;
  }
  if (!value) {
    return QCS_ERR_OK;
  }
  QCS_Circuit x;
  qcs_circuit_create(qubit + 1, &x);
  qcs_circuit_push_gate(&x, qcs_gate_create_x(qubit));
  err = simulator->run(simulator->context, branch->state, &x);
  qcs_circuit_destroy(&x);
  return err;
}

// split the branch's shots at a measure or reset: each draws its value, the
// shots that drew 1 go on in a copy (pushed on pending), the others here
static QCS_Error qcs_simulator_split(const QCS_Simulator *simulator,
                                     QCS_Branch *branch, const QCS_Op *op,
                                     QCS_Random *random, QCS_Branch **pending) {
  uint32_t qubit = op->kind == QCS_OP_MEASURE ? op->measure.qubit : op->qubit;
  double p[2];
  QCS_Error err = simulator->qubit_probabilities(simulator->context,
                                                 branch->state, qubit, p);
  if (err != QCS_ERR_OK) {
    return err;
  }
  double total = p[0] + p[1];
  if (!(total > 0)) {
    fprintf(stderr, "[e] invalid state: total probability %.17g\n", total);
    return QCS_ERR_INVALID_STATE;
  }

  // draw each shot's value, as qcs_state_measure does: a binomial count of 1s
  size_t ones = 0;
  for (size_t s = 0; s < branch->shots; s++) {
    ones += !(qcs_random_next_double(random) * total < p[0]);
  }

  // both values drawn: the 1s in a copy
  if (ones > 0 && ones < branch->shots) {
    QCS_Branch other = *branch;
    other.shots = ones;
    other.state = simulator->copy(simulator->context, branch->state);
    if (!other.state) {
      return QCS_ERR_OUT_OF_MEMORY;
    }
    err = qcs_simulator_settle(simulator, &other, op, qubit, 1, p);
    if (err != QCS_ERR_OK) {
      simulator->destroy(simulator->context, other.state);
      return err;
    }
    arrput(*pending, other);
    branch->shots -= ones;
    ones = 0;
  }
  return qcs_simulator_settle(simulator, branch, op, qubit, ones > 0, p);
}

// run the rest of the top-level ops on the branch's state, sample its shots,
// each one's clbits into clbits from *filled
static QCS_Error qcs_simulator_sample(const QCS_Simulator *simulator,
                                      const QCS_Program *program,
                                      QCS_Branch *branch, QCS_Random *random,
                                      uint64_t *clbits, size_t *filled) {
  // run the gates, see if anything is measured
  size_t first = branch->cursor.frames[0].index;
  bool measured = false;
  for (size_t i = first; i < qcs_ops_count(program->ops); i++) {
    const QCS_Op *op = &program->ops[i];
    if (op->kind == QCS_OP_GATES) {
      QCS_Error err =
          simulator->run(simulator->context, branch->state, &op->gates);
      if (err != QCS_ERR_OK) {
        return err;
      }
    }
    measured = measured || op->kind == QCS_OP_MEASURE;
  }

  // nothing measured: every shot has the branch's clbits
  if (!measured) {
    for (size_t s = 0; s < branch->shots; s++) {
      clbits[(*filled)++] = branch->clbits;
    }
    return QCS_ERR_OK;
  }

  // sample, each measure copies its qubit's bit into its clbit
  size_t *outcomes = malloc(branch->shots * sizeof *outcomes);
  if (!outcomes) {
    fprintf(stderr, "[e] out of memory for %zu shots\n", branch->shots);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  QCS_Error err = simulator->sample(simulator->context, branch->state,
                                    branch->shots, random, outcomes);
  for (size_t s = 0; err == QCS_ERR_OK && s < branch->shots; s++) {
    uint64_t bits = branch->clbits;
    for (size_t i = first; i < qcs_ops_count(program->ops); i++) {
      const QCS_Op *op = &program->ops[i];
      if (op->kind == QCS_OP_MEASURE) {
        bits = qcs_clbits_set(bits, op->measure.clbit,
                              outcomes[s] >> op->measure.qubit & 1);
      }
    }
    clbits[(*filled)++] = bits;
  }
  free(outcomes);
  return err;
}

// run the branch to its end: splits pushed on pending, its shots' clbits
// into clbits from *filled
static QCS_Error qcs_simulator_run_branch(
    const QCS_Simulator *simulator, const QCS_Program *program,
    size_t sample_start, QCS_Branch *branch, QCS_Random *random,
    QCS_Branch **pending, uint64_t *clbits, size_t *filled) {
  QCS_Cursor *cursor = &branch->cursor;
  for (;;) {
    // leave the if bodies that are done
    while (cursor->depth > 1 &&
           cursor->frames[cursor->depth - 1].index >=
               qcs_ops_count(cursor->frames[cursor->depth - 1].ops)) {
      cursor->depth--;
    }

    // at the top level from the sample start on: sample the rest
    if (cursor->depth == 1 && cursor->frames[0].index >= sample_start) {
      return qcs_simulator_sample(simulator, program, branch, random, clbits,
                                  filled);
    }

    // the next op
    const QCS_Op *op = &cursor->frames[cursor->depth - 1]
                            .ops[cursor->frames[cursor->depth - 1].index++];
    QCS_Error err = QCS_ERR_OK;
    switch (op->kind) {
    case QCS_OP_GATES:
      err = simulator->run(simulator->context, branch->state, &op->gates);
      break;
    case QCS_OP_MEASURE:
    case QCS_OP_RESET:
      err = qcs_simulator_split(simulator, branch, op, random, pending);
      break;
    case QCS_OP_IF: {
      // the branch the clbits pick, entered
      bool taken = (branch->clbits & op->branch.mask) == op->branch.value;
      const QCS_Op *ops = taken ? op->branch.then_ops : op->branch.else_ops;
      if (qcs_ops_count(ops) == 0) {
        break;
      }
      if (cursor->depth == QCS_SIMULATOR_MAX_DEPTH) {
        fprintf(stderr, "[e] ifs nest more than %d deep\n",
                QCS_SIMULATOR_MAX_DEPTH);
        return QCS_ERR_INVALID_STATE;
      }
      cursor->frames[cursor->depth].ops = ops;
      cursor->frames[cursor->depth].index = 0;
      cursor->depth++;
      break;
    }
    }
    if (err != QCS_ERR_OK) {
      return err;
    }
  }
}

QCS_Error qcs_simulator_run_program(const QCS_Simulator *simulator,
                                    const QCS_Program *program, size_t shots,
                                    QCS_Random *random, uint64_t *clbits) {
  // one branch with every shot, at the start
  QCS_Branch start = {.cursor = {.frames = {{program->ops, 0}}, .depth = 1},
                      .shots = shots};
  start.state = simulator->create(simulator->context, program->qubit_count);
  if (!start.state) {
    return QCS_ERR_OUT_OF_MEMORY;
  }
  QCS_Branch *pending = nullptr;
  arrput(pending, start);

  // run the branches depth first, until none is left
  size_t sample_start = qcs_ops_sample_start(program->ops);
  size_t filled = 0;
  QCS_Error err = QCS_ERR_OK;
  while (err == QCS_ERR_OK && arrlen(pending) > 0) {
    QCS_Branch branch = arrpop(pending);
    err = qcs_simulator_run_branch(simulator, program, sample_start, &branch,
                                   random, &pending, clbits, &filled);
    simulator->destroy(simulator->context, branch.state);
  }

  // on error, the branches left
  for (ptrdiff_t i = 0; i < arrlen(pending); i++) {
    simulator->destroy(simulator->context, pending[i].state);
  }
  arrfree(pending);
  return err;
}
