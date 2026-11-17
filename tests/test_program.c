// Checks building, dumping and destroying programs: runs, measures, resets,
// ifs and the qubit and clbit counts they grow. No GPU needed.

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <qcs/program.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// A circuit of qubit_count qubits, which must be valid
static QCS_Circuit new_circuit(uint32_t qubit_count) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(qubit_count, &circuit) == QCS_ERR_OK);
  return circuit;
}

// Push gate, which must be accepted
static void push_ok(QCS_Circuit *circuit, QCS_Gate gate) {
  assert(qcs_circuit_push_gate(circuit, gate) == QCS_ERR_OK);
}

static void test_create(void) {
  QCS_Program program = qcs_program_create(3, 2);
  assert(program.qubit_count == 3);
  assert(program.clbit_count == 2);
  assert(program.ops == NULL);
  qcs_program_destroy(&program);
}

// consecutive circuits merge into one run, the caller keeps its circuit
static void test_append_circuit_merge(void) {
  QCS_Program program = qcs_program_create(0, 0);
  QCS_Circuit a = new_circuit(1);
  push_ok(&a, qcs_gate_create_h(0));
  a.global_phase = 0.25;
  QCS_Circuit b = new_circuit(3);
  push_ok(&b, qcs_gate_create_x(2));
  b.global_phase = 0.5;

  qcs_program_append_circuit(&program, &a);
  qcs_program_append_circuit(&program, &b);
  assert(qcs_ops_count(program.ops) == 1);
  assert(program.qubit_count == 3);
  const QCS_Circuit *run = &program.ops[0].gates;
  assert(program.ops[0].kind == QCS_OP_GATES);
  assert(run->qubit_count == 3);
  assert(qcs_circuit_gate_count(run) == 2);
  assert(run->gates[0].target == 0);
  assert(run->gates[1].target == 2);
  assert(qcs_f64_near(run->global_phase, 0.75));

  // the program has its own copy
  assert(run->gates != a.gates);
  assert(qcs_circuit_gate_count(&a) == 1);
  qcs_circuit_destroy(&a);
  qcs_circuit_destroy(&b);
  assert(qcs_circuit_gate_count(run) == 2);
  qcs_program_destroy(&program);
}

// a measure or reset ends the run, the next circuit opens a new one
static void test_append_circuit_split(void) {
  QCS_Program program = qcs_program_create(0, 0);
  QCS_Circuit circuit = new_circuit(2);
  push_ok(&circuit, qcs_gate_create_h(0));

  qcs_program_append_circuit(&program, &circuit);
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  qcs_program_append_circuit(&program, &circuit);
  assert(qcs_program_append_reset(&program, 1) == QCS_ERR_OK);
  qcs_program_append_circuit(&program, &circuit);

  assert(qcs_ops_count(program.ops) == 5);
  assert(program.ops[0].kind == QCS_OP_GATES);
  assert(program.ops[1].kind == QCS_OP_MEASURE);
  assert(program.ops[2].kind == QCS_OP_GATES);
  assert(program.ops[3].kind == QCS_OP_RESET);
  assert(program.ops[4].kind == QCS_OP_GATES);
  assert(qcs_circuit_gate_count(&program.ops[4].gates) == 1);
  qcs_circuit_destroy(&circuit);
  qcs_program_destroy(&program);
}

static void test_append_measure(void) {
  QCS_Program program = qcs_program_create(1, 1);
  assert(qcs_program_append_measure(&program, 4, 6) == QCS_ERR_OK);
  assert(program.qubit_count == 5);
  assert(program.clbit_count == 7);
  assert(program.ops[0].kind == QCS_OP_MEASURE);
  assert(program.ops[0].measure.qubit == 4);
  assert(program.ops[0].measure.clbit == 6);

  // counts never shrink
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  assert(program.qubit_count == 5);
  assert(program.clbit_count == 7);

  // last valid bits
  assert(qcs_program_append_measure(&program, QCS_MAX_QUBITS - 1,
                                    QCS_MAX_CLBITS - 1) == QCS_ERR_OK);
  assert(program.qubit_count == QCS_MAX_QUBITS);
  assert(program.clbit_count == QCS_MAX_CLBITS);
  qcs_program_destroy(&program);
}

// out of range: rejected, nothing appended
static void test_append_measure_invalid(void) {
  QCS_Program program = qcs_program_create(1, 1);
  assert(qcs_program_append_measure(&program, QCS_MAX_QUBITS, 0) ==
         QCS_ERR_MAX_QUBITS);
  assert(qcs_program_append_measure(&program, 0, QCS_MAX_CLBITS) ==
         QCS_ERR_MAX_BITS);
  assert(qcs_ops_count(program.ops) == 0);
  assert(program.qubit_count == 1);
  assert(program.clbit_count == 1);
  qcs_program_destroy(&program);
}

static void test_append_reset(void) {
  QCS_Program program = qcs_program_create(1, 2);
  assert(qcs_program_append_reset(&program, 3) == QCS_ERR_OK);
  assert(program.qubit_count == 4);
  assert(program.clbit_count == 2);
  assert(program.ops[0].kind == QCS_OP_RESET);
  assert(program.ops[0].qubit == 3);

  assert(qcs_program_append_reset(&program, QCS_MAX_QUBITS) ==
         QCS_ERR_MAX_QUBITS);
  assert(qcs_ops_count(program.ops) == 1);
  assert(program.qubit_count == 4);
  qcs_program_destroy(&program);
}

// the branches move into the op, no else is NULL
static void test_append_if(void) {
  QCS_Program program = qcs_program_create(1, 0);
  QCS_Op *then_ops = NULL;
  qcs_ops_append_reset(&then_ops, 0);
  QCS_Op *moved = then_ops;

  assert(qcs_program_append_if(&program, 0b100, 0b100, &then_ops, NULL) ==
         QCS_ERR_OK);
  assert(then_ops == NULL);
  const QCS_Op *op = &program.ops[0];
  assert(op->kind == QCS_OP_IF);
  assert(op->branch.mask == 0b100);
  assert(op->branch.value == 0b100);
  assert(op->branch.then_ops == moved);
  assert(op->branch.else_ops == NULL);
  assert(program.clbit_count == 3);

  // with an else
  QCS_Op *a = NULL, *b = NULL;
  qcs_ops_append_reset(&a, 0);
  qcs_ops_append_reset(&b, 0);
  QCS_Op *moved_else = b;
  assert(qcs_program_append_if(&program, 1, 0, &a, &b) == QCS_ERR_OK);
  assert(a == NULL && b == NULL);
  assert(program.ops[1].branch.else_ops == moved_else);
  qcs_program_destroy(&program);
}

// mask on the last clbit: 64 clbits, no overflow
static void test_append_if_last_clbit(void) {
  QCS_Program program = qcs_program_create(1, 0);
  QCS_Op *then_ops = NULL;
  assert(qcs_program_append_if(&program, 1ull << 63, 0, &then_ops, NULL) ==
         QCS_ERR_OK);
  assert(program.clbit_count == 64);
  qcs_program_destroy(&program);
}

// value outside mask: rejected, the caller keeps its branches
static void test_append_if_value_outside_mask(void) {
  QCS_Program program = qcs_program_create(1, 0);
  QCS_Op *then_ops = NULL;
  qcs_ops_append_reset(&then_ops, 0);
  assert(qcs_program_append_if(&program, 0b01, 0b10, &then_ops, NULL) ==
         QCS_ERR_MAX_BITS);
  assert(then_ops != NULL);
  assert(qcs_ops_count(program.ops) == 0);
  assert(program.clbit_count == 0);
  qcs_ops_destroy(&then_ops);
  qcs_program_destroy(&program);
}

// bits used inside branches, at any depth, grow the program
static void test_append_if_nested_growth(void) {
  QCS_Program program = qcs_program_create(1, 1);
  QCS_Circuit circuit = new_circuit(5);
  push_ok(&circuit, qcs_gate_create_x(4));

  // if (c0) { if (c1) { measure q7 -> c9 } } else { x q4 }
  QCS_Op *deep = NULL, *mid = NULL, *else_ops = NULL;
  qcs_ops_append_measure(&deep, 7, 9);
  qcs_ops_append_if(&mid, 0b10, 0b10, &deep, NULL);
  qcs_ops_append_circuit(&else_ops, &circuit);
  assert(qcs_program_append_if(&program, 1, 1, &mid, &else_ops) == QCS_ERR_OK);
  assert(program.qubit_count == 8);
  assert(program.clbit_count == 10);
  qcs_circuit_destroy(&circuit);
  qcs_program_destroy(&program);
}

// too many bits inside a branch: rejected, nothing changes
static void test_append_if_branch_too_wide(void) {
  QCS_Program program = qcs_program_create(2, 1);
  QCS_Op *then_ops = NULL, *else_ops = NULL;
  qcs_ops_append_reset(&then_ops, QCS_MAX_QUBITS);
  assert(qcs_program_append_if(&program, 1, 1, &then_ops, NULL) ==
         QCS_ERR_MAX_QUBITS);
  assert(then_ops != NULL);

  qcs_ops_append_measure(&else_ops, 0, QCS_MAX_CLBITS);
  assert(qcs_program_append_if(&program, 1, 1, &then_ops, &else_ops) ==
         QCS_ERR_MAX_QUBITS);
  qcs_ops_destroy(&then_ops);
  assert(qcs_program_append_if(&program, 1, 1, &then_ops, &else_ops) ==
         QCS_ERR_MAX_BITS);
  assert(else_ops != NULL);

  assert(qcs_ops_count(program.ops) == 0);
  assert(program.qubit_count == 2);
  assert(program.clbit_count == 1);
  qcs_ops_destroy(&else_ops);
  qcs_program_destroy(&program);
}

// program dumped by qcs_program_fdump is exactly expected
static void expect_dump(const QCS_Program *program, const char *expected) {
  // dump into a temporary file
  FILE *file = tmpfile();
  assert(file);
  qcs_program_fdump(program, file);

  // read it back
  char text[512];
  rewind(file);
  size_t length = fread(text, 1, sizeof text - 1, file);
  text[length] = '\0';
  fclose(file);
  if (strcmp(text, expected) != 0) {
    fprintf(stderr, "[e] dumped:\n%s\nexpected:\n%s\n", text, expected);
    assert(false);
  }
}

// counts, then ops in order, branches indented, an empty else left out
static void test_fdump(void) {
  QCS_Program program = qcs_program_create(0, 0);
  expect_dump(&program, "qubits 0, clbits 0\n");

  QCS_Circuit h = new_circuit(1);
  push_ok(&h, qcs_gate_create_h(0));
  h.global_phase = 0.5;
  QCS_Circuit x = new_circuit(2);
  push_ok(&x, qcs_gate_create_x(1));

  // if (c0 && !c1) { x q1; if (!c1) { reset q0 } } else { reset q1 }
  QCS_Op *inner = NULL, *then_ops = NULL, *else_ops = NULL;
  qcs_ops_append_reset(&inner, 0);
  qcs_ops_append_circuit(&then_ops, &x);
  qcs_ops_append_if(&then_ops, 0b10, 0, &inner, NULL);
  qcs_ops_append_reset(&else_ops, 1);

  // if (true) { measure q1 -> c2 }, with an empty else
  QCS_Op *always = NULL, *empty = NULL;
  qcs_ops_append_measure(&always, 1, 2);

  qcs_program_append_circuit(&program, &h);
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  assert(qcs_program_append_if(&program, 0b11, 0b01, &then_ops, &else_ops) ==
         QCS_ERR_OK);
  assert(qcs_program_append_if(&program, 0, 0, &always, &empty) == QCS_ERR_OK);
  expect_dump(&program, "qubits 2, clbits 3\n"
                        "gphase(0.5)\n"
                        "h q[0]\n"
                        "measure q[0] -> c[0]\n"
                        "if c[0] == 1 && c[1] == 0 {\n"
                        "  x q[1]\n"
                        "  if c[1] == 0 {\n"
                        "    reset q[0]\n"
                        "  }\n"
                        "} else {\n"
                        "  reset q[1]\n"
                        "}\n"
                        "if true {\n"
                        "  measure q[1] -> c[2]\n"
                        "}\n");
  qcs_circuit_destroy(&h);
  qcs_circuit_destroy(&x);
  qcs_program_destroy(&program);
}

// A one-gate circuit, as wide as the gate's highest qubit
static QCS_Circuit gate_circuit(QCS_Gate gate) {
  uint32_t qubits = 1u << gate.target | gate.control_ones | gate.control_zeros;
  uint32_t width = 0;
  while (width < QCS_MAX_QUBITS && qubits >> width) {
    width++;
  }
  QCS_Circuit circuit = new_circuit(width);
  push_ok(&circuit, gate);
  return circuit;
}

// append gate to the program as a one-gate circuit
static void append_gate(QCS_Program *program, QCS_Gate gate) {
  QCS_Circuit circuit = gate_circuit(gate);
  qcs_program_append_circuit(program, &circuit);
  qcs_circuit_destroy(&circuit);
}

// append gate to an op list as a one-gate circuit
static void ops_append_gate(QCS_Op **ops, QCS_Gate gate) {
  QCS_Circuit circuit = gate_circuit(gate);
  qcs_ops_append_circuit(ops, &circuit);
  qcs_circuit_destroy(&circuit);
}

// append a measure, which must be accepted
static void measure_ok(QCS_Program *program, uint32_t qubit, uint32_t clbit) {
  assert(qcs_program_append_measure(program, qubit, clbit) == QCS_ERR_OK);
}

// gate after measuring its target leaves the sampling path
static void expect_no_sample(uint32_t qubit, QCS_Gate gate) {
  QCS_Program program = qcs_program_create(0, 0);
  measure_ok(&program, qubit, 0);
  append_gate(&program, gate);
  assert(!qcs_program_can_sample(&program));
  qcs_program_destroy(&program);
}

// sampling stays exact: other qubits, measured controls, diagonal targets
static void test_can_sample(void) {
  QCS_Program program = qcs_program_create(3, 2);
  assert(qcs_program_can_sample(&program));

  // Bell, measured
  append_gate(&program, qcs_gate_create_h(0));
  append_gate(&program, qcs_gate_create_cx(1, 0));
  assert(qcs_program_can_sample(&program));
  measure_ok(&program, 0, 0);
  assert(qcs_program_can_sample(&program));

  // an unmeasured target, a measured control
  append_gate(&program, qcs_gate_create_x(2));
  append_gate(&program, qcs_gate_create_cx(2, 0));
  assert(qcs_program_can_sample(&program));

  // diagonal on the measured qubit, controlled too
  append_gate(&program, qcs_gate_create_z(0));
  append_gate(&program, qcs_gate_create_rz(0, 0.3));
  append_gate(&program, qcs_gate_create_cz(0, 1));
  assert(qcs_program_can_sample(&program));

  // measured again
  measure_ok(&program, 0, 1);
  measure_ok(&program, 1, 0);
  assert(qcs_program_can_sample(&program));
  qcs_program_destroy(&program);
}

// non-diagonal on a measured target, any reset or if: per shot
static void test_cannot_sample(void) {
  expect_no_sample(0, qcs_gate_create_x(0));
  expect_no_sample(0, qcs_gate_create_h(0));
  expect_no_sample(0, qcs_gate_create_cx(0, 1));
  expect_no_sample(QCS_MAX_QUBITS - 1, qcs_gate_create_x(QCS_MAX_QUBITS - 1));

  // reset with nothing measured
  QCS_Program program = qcs_program_create(1, 0);
  assert(qcs_program_append_reset(&program, 0) == QCS_ERR_OK);
  assert(!qcs_program_can_sample(&program));
  qcs_program_destroy(&program);

  // if, always true and empty
  program = qcs_program_create(1, 0);
  QCS_Op *then_ops = NULL;
  assert(qcs_program_append_if(&program, 0, 0, &then_ops, NULL) == QCS_ERR_OK);
  assert(!qcs_program_can_sample(&program));
  qcs_program_destroy(&program);
}

enum { SHOTS = 10000 };

// count within 5σ of a binomial(SHOTS, p)
static bool near_expected(size_t count, double p) {
  double mean = SHOTS * p;
  double sigma = sqrt(SHOTS * p * (1 - p));
  return fabs((double)count - mean) <= 5 * sigma;
}

// run program SHOTS times, count each clbits value (below 8), destroy it
static void run_counts(QCS_Program *program, size_t counts[8]) {
  static uint64_t clbits[SHOTS];
  QCS_Random random = qcs_random_create(7);
  assert(qcs_program_run(program, SHOTS, &random, clbits) == QCS_ERR_OK);
  for (size_t k = 0; k < 8; k++) {
    counts[k] = 0;
  }
  for (size_t s = 0; s < SHOTS; s++) {
    assert(clbits[s] < 8);
    counts[clbits[s]]++;
  }
  qcs_program_destroy(program);
}

// Bell, measured at the end: sampled, 00 and 11 half each
static void test_run_bell(void) {
  QCS_Program program = qcs_program_create(2, 2);
  append_gate(&program, qcs_gate_create_h(0));
  append_gate(&program, qcs_gate_create_cx(1, 0));
  measure_ok(&program, 0, 0);
  measure_ok(&program, 1, 1);
  assert(qcs_program_can_sample(&program));

  size_t counts[8];
  run_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5));
  assert(near_expected(counts[0b11], 0.5));
  assert(counts[0b01] == 0 && counts[0b10] == 0);
}

// a measured control: still sampled, the same as Bell
static void test_run_measured_control(void) {
  QCS_Program program = qcs_program_create(2, 2);
  append_gate(&program, qcs_gate_create_h(0));
  measure_ok(&program, 0, 0);
  append_gate(&program, qcs_gate_create_cx(1, 0));
  measure_ok(&program, 1, 1);
  assert(qcs_program_can_sample(&program));

  size_t counts[8];
  run_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5));
  assert(near_expected(counts[0b11], 0.5));
  assert(counts[0b01] == 0 && counts[0b10] == 0);
}

// x after measuring its qubit: per shot, c1 is always !c0
static void test_run_x_after_measure(void) {
  QCS_Program program = qcs_program_create(1, 2);
  append_gate(&program, qcs_gate_create_h(0));
  measure_ok(&program, 0, 0);
  append_gate(&program, qcs_gate_create_x(0));
  measure_ok(&program, 0, 1);
  assert(!qcs_program_can_sample(&program));

  size_t counts[8];
  run_counts(&program, counts);
  assert(near_expected(counts[0b01], 0.5));
  assert(near_expected(counts[0b10], 0.5));
  assert(counts[0b00] == 0 && counts[0b11] == 0);
}

// measuring into the same clbit again overwrites it, 1 back to 0 too
static void test_run_clbit_overwrite(void) {
  QCS_Program program = qcs_program_create(2, 1);
  append_gate(&program, qcs_gate_create_x(0));
  measure_ok(&program, 0, 0);
  measure_ok(&program, 1, 0);
  append_gate(&program, qcs_gate_create_x(0));
  assert(!qcs_program_can_sample(&program));

  size_t counts[8];
  run_counts(&program, counts);
  assert(counts[0] == SHOTS);
}

// reset of |1⟩ measures 0
static void test_run_reset(void) {
  QCS_Program program = qcs_program_create(1, 1);
  append_gate(&program, qcs_gate_create_x(0));
  assert(qcs_program_append_reset(&program, 0) == QCS_ERR_OK);
  measure_ok(&program, 0, 0);

  size_t counts[8];
  run_counts(&program, counts);
  assert(counts[0] == SHOTS);
}

// if (c0) x q0 resets q0 by hand: c1 always 0
static void test_run_if(void) {
  QCS_Program program = qcs_program_create(1, 2);
  append_gate(&program, qcs_gate_create_h(0));
  measure_ok(&program, 0, 0);
  QCS_Op *then_ops = NULL;
  ops_append_gate(&then_ops, qcs_gate_create_x(0));
  assert(qcs_program_append_if(&program, 1, 1, &then_ops, NULL) == QCS_ERR_OK);
  measure_ok(&program, 0, 1);

  size_t counts[8];
  run_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5));
  assert(near_expected(counts[0b01], 0.5));
  assert(counts[0b10] == 0 && counts[0b11] == 0);
}

// if (c0 == 0) takes the else for 1: q1 copies c0
static void test_run_if_else(void) {
  QCS_Program program = qcs_program_create(2, 2);
  append_gate(&program, qcs_gate_create_h(0));
  measure_ok(&program, 0, 0);
  QCS_Op *then_ops = NULL, *else_ops = NULL;
  ops_append_gate(&then_ops, qcs_gate_create_z(1));
  ops_append_gate(&else_ops, qcs_gate_create_x(1));
  assert(qcs_program_append_if(&program, 1, 0, &then_ops, &else_ops) ==
         QCS_ERR_OK);
  measure_ok(&program, 1, 1);

  size_t counts[8];
  run_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5));
  assert(near_expected(counts[0b11], 0.5));
  assert(counts[0b01] == 0 && counts[0b10] == 0);
}

// teleport |1⟩: q2 always 1, (c0, c1) a quarter each
static void test_run_teleport(void) {
  QCS_Program program = qcs_program_create(3, 3);
  append_gate(&program, qcs_gate_create_x(0));
  append_gate(&program, qcs_gate_create_h(1));
  append_gate(&program, qcs_gate_create_cx(2, 1));
  append_gate(&program, qcs_gate_create_cx(1, 0));
  append_gate(&program, qcs_gate_create_h(0));
  measure_ok(&program, 0, 0);
  measure_ok(&program, 1, 1);

  // if (c1) x q2; if (c0) z q2
  QCS_Op *fix_x = NULL, *fix_z = NULL;
  ops_append_gate(&fix_x, qcs_gate_create_x(2));
  ops_append_gate(&fix_z, qcs_gate_create_z(2));
  assert(qcs_program_append_if(&program, 0b10, 0b10, &fix_x, NULL) ==
         QCS_ERR_OK);
  assert(qcs_program_append_if(&program, 0b01, 0b01, &fix_z, NULL) ==
         QCS_ERR_OK);
  measure_ok(&program, 2, 2);

  size_t counts[8];
  run_counts(&program, counts);
  for (size_t k = 0; k < 4; k++) {
    assert(counts[k] == 0);
    assert(near_expected(counts[0b100 | k], 0.25));
  }
}

// a qubit past the state is an error, not a crash
static void test_run_invalid(void) {
  QCS_Program program = qcs_program_create(1, 1);
  QCS_Op *then_ops = NULL;
  qcs_ops_append_reset(&then_ops, 0);
  assert(qcs_program_append_if(&program, 0, 0, &then_ops, NULL) == QCS_ERR_OK);
  program.qubit_count = 0;
  uint64_t clbits[1];
  QCS_Random random = qcs_random_create(7);
  assert(qcs_program_run(&program, 1, &random, clbits) == QCS_ERR_QUBIT_RANGE);
  qcs_program_destroy(&program);
}

// nested runs and branches are freed, the program is emptied
static void test_destroy(void) {
  QCS_Program program = qcs_program_create(0, 0);
  QCS_Circuit circuit = new_circuit(2);
  push_ok(&circuit, qcs_gate_create_h(1));

  // gates; if (c0) { gates; if (c1) { gates } } else { reset q1 }
  QCS_Op *inner = NULL, *outer = NULL, *else_ops = NULL;
  qcs_ops_append_circuit(&inner, &circuit);
  qcs_ops_append_circuit(&outer, &circuit);
  qcs_ops_append_if(&outer, 0b10, 0b10, &inner, NULL);
  qcs_ops_append_reset(&else_ops, 1);
  qcs_program_append_circuit(&program, &circuit);
  assert(qcs_program_append_if(&program, 1, 1, &outer, &else_ops) ==
         QCS_ERR_OK);
  qcs_circuit_destroy(&circuit);

  qcs_program_destroy(&program);
  assert(program.ops == NULL);
  assert(program.qubit_count == 0);
  assert(program.clbit_count == 0);

  // destroying twice, or an empty list, is a no-op
  qcs_program_destroy(&program);
  QCS_Op *empty = NULL;
  qcs_ops_destroy(&empty);
  assert(empty == NULL);
}

int main(void) {
  test_create();
  test_append_circuit_merge();
  test_append_circuit_split();
  test_append_measure();
  test_append_measure_invalid();
  test_append_reset();
  test_append_if();
  test_append_if_last_clbit();
  test_append_if_value_outside_mask();
  test_append_if_nested_growth();
  test_append_if_branch_too_wide();
  test_fdump();
  test_can_sample();
  test_cannot_sample();
  test_run_bell();
  test_run_measured_control();
  test_run_x_after_measure();
  test_run_clbit_overwrite();
  test_run_reset();
  test_run_if();
  test_run_if_else();
  test_run_teleport();
  test_run_invalid();
  test_destroy();
  return 0;
}
