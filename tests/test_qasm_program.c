// Checks OpenQASM programs: registers, constants, gate calls, broadcasting,
// the stdgates.inc table, errors with positions

#undef NDEBUG
#include "qasm/qcs_qasm_parser.h"
#include "qasm/qcs_qasm_program.h"
#include <assert.h>
#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STDGATES "include \"stdgates.inc\";\n"

// source parsed and turned into a program
static QCS_QasmProgram expect_program(const char *source) {
  QCS_Arena arena = {0};
  QCS_QasmStmt *statements;
  QCS_QasmProgram program;
  QCS_QasmError error;
  if (!qcs_qasm_parse_program(source, &arena, &statements, &error) ||
      !qcs_qasm_program_create(statements, nullptr, 0, &program, &error)) {
    fprintf(stderr, "[e] %s\n%u:%u: %s\n", source, error.line, error.column,
            error.message);
    assert(false);
  }
  qcs_arena_free(&arena);
  return program;
}

// source parses, but its program fails at line:column and is left empty
static void expect_error(const char *source, uint32_t line, uint32_t column) {
  QCS_Arena arena = {0};
  QCS_QasmStmt *statements;
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse_program(source, &arena, &statements, &error));
  assert(!qcs_qasm_program_create(statements, nullptr, 0, &program, &error));
  if (error.line != line || error.column != column) {
    fprintf(stderr, "[e] %s\nerror at %u:%u (%s), expected %u:%u\n", source,
            error.line, error.column, error.message, line, column);
    assert(false);
  }
  assert(strlen(error.message) > 0);
  assert(program.program.ops == nullptr && program.qregs == nullptr &&
         program.cregs == nullptr && program.program.qubit_count == 0);
  qcs_arena_free(&arena);
}

// the gates of a program without measures: its one run, or none
static const QCS_Circuit *run_of(const QCS_QasmProgram *program) {
  static const QCS_Circuit none = {0};
  size_t count = qcs_ops_count(program->program.ops);
  assert(count <= 1);
  if (count == 0) {
    return &none;
  }
  assert(program->program.ops[0].kind == QCS_OP_GATES);
  return &program->program.ops[0].gates;
}

// Same matrix, target and controls
static bool gate_is(QCS_Gate a, QCS_Gate b) {
  return qcs_gate_matrix_equal(a, b) && a.target == b.target &&
         a.control_ones == b.control_ones && a.control_zeros == b.control_zeros;
}

static QCS_Gate with_control(QCS_Gate gate, uint32_t control) {
  qcs_gate_add_control_one(&gate, control);
  return gate;
}

static QCS_Gate with_control_zero(QCS_Gate gate, uint32_t control) {
  qcs_gate_add_control_zero(&gate, control);
  return gate;
}

// call on qubit[3] q builds exactly the expected gates
static void expect_gates(const char *call, const QCS_Gate *expected,
                         size_t count) {
  char source[256];
  snprintf(source, sizeof source, STDGATES "qubit[3] q;\n%s", call);
  QCS_QasmProgram program = expect_program(source);
  if (qcs_circuit_gate_count(run_of(&program)) != count) {
    fprintf(stderr, "[e] %s: %zu gates, expected %zu\n", call,
            qcs_circuit_gate_count(run_of(&program)), count);
    assert(false);
  }
  for (size_t i = 0; i < count; i++) {
    if (!gate_is(run_of(&program)->gates[i], expected[i])) {
      fprintf(stderr, "[e] %s: gate %zu differs\n", call, i);
      assert(false);
    }
  }
  qcs_qasm_program_destroy(&program);
}

// call on qubit[3] q makes no gate, only the global phase phase
static void expect_phase(const char *call, double phase) {
  char source[256];
  snprintf(source, sizeof source, STDGATES "qubit[3] q;\n%s", call);
  QCS_QasmProgram program = expect_program(source);
  if (qcs_circuit_gate_count(run_of(&program)) != 0 ||
      !qcs_f64_near(run_of(&program)->global_phase, phase)) {
    fprintf(stderr, "[e] %s: %zu gates, phase %g, expected %g\n", call,
            qcs_circuit_gate_count(run_of(&program)),
            run_of(&program)->global_phase, phase);
    assert(false);
  }
  qcs_qasm_program_destroy(&program);
}

// rz(expr) on one qubit has the angle value
static void expect_angle(const char *expr, double value) {
  char source[256];
  snprintf(source, sizeof source, STDGATES "qubit q;\nrz(%s) q;", expr);
  QCS_QasmProgram program = expect_program(source);
  if (!gate_is(run_of(&program)->gates[0], qcs_gate_create_rz(0, value))) {
    fprintf(stderr, "[e] rz(%s): angle %g, expected %g\n", expr,
            run_of(&program)->gates[0].params[0], value);
    assert(false);
  }
  qcs_qasm_program_destroy(&program);
}

// Registers at consecutive offsets, qubits and bits counted apart
static void test_registers(void) {
  QCS_QasmProgram program =
      expect_program("qubit[2] a; qubit[3] b; bit[4] c; qubit s; bit d;");
  assert(program.program.qubit_count == 6);
  assert(program.program.clbit_count == 5);
  assert(strcmp(program.qregs[1].name, "b") == 0);
  assert(program.qregs[1].offset == 2 && program.qregs[1].size == 3);
  assert(strcmp(program.qregs[2].name, "s") == 0);
  assert(program.qregs[2].offset == 5 && program.qregs[2].size == 1);
  assert(program.cregs[1].offset == 4 && program.cregs[1].size == 1);
  qcs_qasm_program_destroy(&program);

  program = expect_program("const int n = 3; qubit[n] q;");
  assert(program.program.qubit_count == 3);
  qcs_qasm_program_destroy(&program);

  program = expect_program("qreg q[2]; creg c[1];");
  assert(program.program.qubit_count == 2 && program.program.clbit_count == 1);
  qcs_qasm_program_destroy(&program);

  // the maximum
  program = expect_program("qubit[32] q; bit[64] c;");
  assert(program.program.qubit_count == 32 &&
         program.program.clbit_count == 64);
  qcs_qasm_program_destroy(&program);
}

// Register errors at the size or the name
static void test_register_errors(void) {
  expect_error("qubit[0] q;", 1, 7);
  expect_error("qubit[2.5] q;", 1, 7);
  expect_error("qubit[33] q;", 1, 7);
  expect_error("qubit[20] a;\nqubit[13] b;", 2, 7);
  expect_error("bit[65] c;", 1, 5);
  expect_error("qubit q; bit q;", 1, 14);
  expect_error("const int pi = 3;", 1, 11);
  expect_error("qubit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa;", 1, 7);
}

// Expressions: integer and float rules, constants, functions
static void test_values(void) {
  expect_angle("7 / 2", 3);
  expect_angle("7 / 2.0", 3.5);
  expect_angle("2 ** 10", 1024);
  expect_angle("2 ** 3 ** 2", 512);
  expect_angle("2 ** -1", 0.5);
  expect_angle("-7 % 3", -1);
  expect_angle("pi", QCS_PI);
  expect_angle("\xcf\x84", 2 * QCS_PI);
  expect_angle("sin(pi / 2)", 1);
  expect_angle("-(1 + 2) * 3", -9);

  QCS_QasmProgram program = expect_program(STDGATES "const float a = 1;\n"
                                                    "const int n = 2;\n"
                                                    "qubit q;\n"
                                                    "rz(a / 2 + n) q;");
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_rz(0, 2.5)));
  qcs_qasm_program_destroy(&program);
}

// Expression errors at the operator, function or name
static void test_value_errors(void) {
  expect_error(STDGATES "qubit q;\nrz(1 / 0) q;", 3, 6);
  expect_error(STDGATES "qubit q;\nrz(1.0 / 0) q;", 3, 8);
  expect_error(STDGATES "qubit q;\nrz(log(0)) q;", 3, 4);
  expect_error(STDGATES "qubit q;\nrz(9223372036854775807 + 1) q;", 3, 24);
  expect_error(STDGATES "qubit q;\nrz(1.5 % 2) q;", 3, 8);
  expect_error(STDGATES "qubit q;\nrz(foo) q;", 3, 4);
  expect_error(STDGATES "qubit q;\nrz(q) q;", 3, 4);
  expect_error("const uint u = -1;", 1, 16);
  expect_error("const int x = 1.5;", 1, 15);
}

// Gate calls on global qubits, U without the include
static void test_calls(void) {
  QCS_QasmProgram program = expect_program(STDGATES "qubit[2] a; qubit[3] b;\n"
                                                    "x b[0];\n"
                                                    "cx a[1], b[2];");
  assert(qcs_circuit_gate_count(run_of(&program)) == 2);
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_x(2)));
  assert(gate_is(run_of(&program)->gates[1], qcs_gate_create_cx(4, 1)));
  qcs_qasm_program_destroy(&program);

  program = expect_program("qubit q; U(1, 2, 3) q;");
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_u(0, 1, 2, 3)));
  assert(strcmp(run_of(&program)->gates[0].name, "U") == 0);
  qcs_qasm_program_destroy(&program);
}

// Call errors: unknown gates, counts, operands, at the name or operand
static void test_call_errors(void) {
  // stdgates.inc not included, then included twice
  expect_error("qubit q; h q;", 1, 10);
  expect_error(STDGATES STDGATES, 2, 1);
  expect_error(STDGATES "qubit q;\nfoo q;", 3, 1);
  // counts
#define Q STDGATES "qubit[3] q; qubit one; bit[2] c;\n"
  expect_error(Q "rz q[0];", 3, 1);
  expect_error(Q "h(1) q[0];", 3, 1);
  expect_error(Q "cx q[0];", 3, 1);
  expect_error(Q "h q[0], q[1];", 3, 1);
  // operands
  expect_error(Q "h c[0];", 3, 3);
  expect_error(Q "h r;", 3, 3);
  expect_error(Q "h q[3];", 3, 5);
  expect_error(Q "h one[0];", 3, 3);
  expect_error(Q "cx q[0], q[0];", 3, 10);
  expect_error(Q "cx q, q;", 3, 7);
  // slices
  expect_error(Q "h q[0:9];", 3, 7);
  expect_error(Q "h q[0:0:2];", 3, 7);
  expect_error(Q "h q[2:0];", 3, 5);
  expect_error(Q "h q[1:2:0];", 3, 5);
#undef Q
}

// Registers and slices broadcast; single qubits repeat
static void test_broadcast(void) {
  expect_gates("h q;",
               (QCS_Gate[]){qcs_gate_create_h(0), qcs_gate_create_h(1),
                            qcs_gate_create_h(2)},
               3);
  expect_gates("h q[0:1];",
               (QCS_Gate[]){qcs_gate_create_h(0), qcs_gate_create_h(1)}, 2);
  expect_gates("h q[2:-1:0];",
               (QCS_Gate[]){qcs_gate_create_h(2), qcs_gate_create_h(1),
                            qcs_gate_create_h(0)},
               3);
  expect_gates("h q[0:2:2];",
               (QCS_Gate[]){qcs_gate_create_h(0), qcs_gate_create_h(2)}, 2);

  QCS_QasmProgram program =
      expect_program(STDGATES "qubit[3] q; qubit[2] a; qubit[2] b;\n"
                              "cx a, b;\n"
                              "cx a[0], b;");
  assert(qcs_circuit_gate_count(run_of(&program)) == 4);
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_cx(5, 3)));
  assert(gate_is(run_of(&program)->gates[1], qcs_gate_create_cx(6, 4)));
  assert(gate_is(run_of(&program)->gates[2], qcs_gate_create_cx(5, 3)));
  assert(gate_is(run_of(&program)->gates[3], qcs_gate_create_cx(6, 3)));
  qcs_qasm_program_destroy(&program);

  expect_error(STDGATES "qubit[3] q; qubit[2] a;\ncx q, a;", 3, 7);
}

// Every row of stdgates.inc against its standard matrices, operands out of
// order so control and target can't be swapped unnoticed
static void test_stdgates(void) {
  const double pi = QCS_PI;
  expect_gates("id q[1];", (QCS_Gate[]){qcs_gate_create_id(1)}, 1);
  expect_gates("x q[1];", (QCS_Gate[]){qcs_gate_create_x(1)}, 1);
  expect_gates("y q[1];", (QCS_Gate[]){qcs_gate_create_y(1)}, 1);
  expect_gates("z q[1];", (QCS_Gate[]){qcs_gate_create_z(1)}, 1);
  expect_gates("h q[1];", (QCS_Gate[]){qcs_gate_create_h(1)}, 1);
  expect_gates("sx q[1];", (QCS_Gate[]){qcs_gate_create_sx(1)}, 1);
  expect_gates("s q[1];", (QCS_Gate[]){qcs_gate_create_p(1, pi / 2)}, 1);
  expect_gates("sdg q[1];", (QCS_Gate[]){qcs_gate_create_p(1, -pi / 2)}, 1);
  expect_gates("t q[1];", (QCS_Gate[]){qcs_gate_create_p(1, pi / 4)}, 1);
  expect_gates("tdg q[1];", (QCS_Gate[]){qcs_gate_create_p(1, -pi / 4)}, 1);
  expect_gates("p(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_p(1, 0.3)}, 1);
  expect_gates("phase(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_p(1, 0.3)}, 1);
  expect_gates("u1(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_p(1, 0.3)}, 1);
  expect_gates("rx(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_rx(1, 0.3)}, 1);
  expect_gates("ry(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_ry(1, 0.3)}, 1);
  expect_gates("rz(0.3) q[1];", (QCS_Gate[]){qcs_gate_create_rz(1, 0.3)}, 1);
  // u2 and u3: the OpenQASM 2 U, RZ(φ)·RY(θ)·RZ(λ)
  expect_gates(
      "u2(1, 2) q[1];",
      (QCS_Gate[]){qcs_gate_mul(qcs_gate_create_rz(1, 1),
                                qcs_gate_mul(qcs_gate_create_ry(1, pi / 2),
                                             qcs_gate_create_rz(1, 2)))},
      1);
  expect_gates(
      "u3(1, 2, 3) q[1];",
      (QCS_Gate[]){qcs_gate_mul(
          qcs_gate_create_rz(1, 2),
          qcs_gate_mul(qcs_gate_create_ry(1, 1), qcs_gate_create_rz(1, 3)))},
      1);

  // control first: q[2] controls q[0]
  expect_gates("cx q[2], q[0];", (QCS_Gate[]){qcs_gate_create_cx(0, 2)}, 1);
  expect_gates("CX q[2], q[0];", (QCS_Gate[]){qcs_gate_create_cx(0, 2)}, 1);
  expect_gates("cy q[2], q[0];", (QCS_Gate[]){qcs_gate_create_cy(0, 2)}, 1);
  expect_gates("cz q[2], q[0];", (QCS_Gate[]){qcs_gate_create_cz(0, 2)}, 1);
  expect_gates("ch q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_h(0), 2)}, 1);
  expect_gates("cp(0.3) q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_p(0, 0.3), 2)}, 1);
  expect_gates("cphase(0.3) q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_p(0, 0.3), 2)}, 1);
  expect_gates("crx(0.3) q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_rx(0, 0.3), 2)}, 1);
  expect_gates("cry(0.3) q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_ry(0, 0.3), 2)}, 1);
  expect_gates("crz(0.3) q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_rz(0, 0.3), 2)}, 1);
  // cu: the file's p(γ − θ/2) a; ctrl @ U(θ, φ, λ) a, b;
  expect_gates("cu(1, 2, 3, 4) q[2], q[0];",
               (QCS_Gate[]){qcs_gate_create_p(2, 4 - 0.5),
                            with_control(qcs_gate_create_u(0, 1, 2, 3), 2)},
               2);
  expect_gates(
      "ccx q[2], q[0], q[1];",
      (QCS_Gate[]){with_control(with_control(qcs_gate_create_x(1), 2), 0)}, 1);
  expect_gates("swap q[2], q[0];",
               (QCS_Gate[]){qcs_gate_create_cx(0, 2), qcs_gate_create_cx(2, 0),
                            qcs_gate_create_cx(0, 2)},
               3);
  expect_gates("cswap q[1], q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_cx(0, 2), 1),
                            with_control(qcs_gate_create_cx(2, 0), 1),
                            with_control(qcs_gate_create_cx(0, 2), 1)},
               3);
}

// state of call, run on qubit[4] q from a random state, after the
// definition def
static QCS_State run_call(const char *def, const char *call) {
  char source[512];
  snprintf(source, sizeof source, STDGATES "qubit[4] q;\n%s\n%s", def, call);
  QCS_QasmProgram program = expect_program(source);
  QCS_Random random = qcs_random_create(7);
  QCS_State state;
  assert(qcs_state_create_random(4, &random, &state) == QCS_ERR_OK);
  assert(qcs_circuit_run(run_of(&program), &state) == QCS_ERR_OK);
  qcs_qasm_program_destroy(&program);
  return state;
}

// Every row of stdgates.inc against the file's own body, both under ctrl @
// so global phases show; s, sdg, t, tdg and sx are pow(0.5) @, outside the
// subset
static void test_stdgates_file(void) {
  static const struct {
    const char *row, *params, *qubits;
    const char *def; // the body, as gate <row>_def
  } rows[] = {
      {"p", "(0.3)", "q[1]", "gate p_def(l) a { ctrl @ gphase(l) a; }"},
      {"x", "", "q[1]", "gate x_def a { U(π, 0, π) a; gphase(-π/2); }"},
      {"y", "", "q[1]", "gate y_def a { U(π, π/2, π/2) a; gphase(-π/2); }"},
      {"z", "", "q[1]", "gate z_def a { p(π) a; }"},
      {"h", "", "q[1]", "gate h_def a { U(π/2, 0, π) a; gphase(-π/4); }"},
      {"rx", "(0.3)", "q[1]",
       "gate rx_def(t) a { U(t, -π/2, π/2) a; gphase(-t/2); }"},
      {"ry", "(0.3)", "q[1]",
       "gate ry_def(t) a { U(t, 0, 0) a; gphase(-t/2); }"},
      {"rz", "(0.3)", "q[1]",
       "gate rz_def(l) a { gphase(-l/2); U(0, 0, l) a; }"},
      {"cx", "", "q[2], q[1]", "gate cx_def a, b { ctrl @ x a, b; }"},
      {"cy", "", "q[2], q[1]", "gate cy_def a, b { ctrl @ y a, b; }"},
      {"cz", "", "q[2], q[1]", "gate cz_def a, b { ctrl @ z a, b; }"},
      {"cp", "(0.3)", "q[2], q[1]",
       "gate cp_def(l) a, b { ctrl @ p(l) a, b; }"},
      {"crx", "(0.3)", "q[2], q[1]",
       "gate crx_def(t) a, b { ctrl @ rx(t) a, b; }"},
      {"cry", "(0.3)", "q[2], q[1]",
       "gate cry_def(t) a, b { ctrl @ ry(t) a, b; }"},
      {"crz", "(0.3)", "q[2], q[1]",
       "gate crz_def(t) a, b { ctrl @ rz(t) a, b; }"},
      {"ch", "", "q[2], q[1]", "gate ch_def a, b { ctrl @ h a, b; }"},
      {"swap", "", "q[2], q[1]",
       "gate swap_def a, b { cx a, b; cx b, a; cx a, b; }"},
      {"ccx", "", "q[3], q[1], q[2]",
       "gate ccx_def a, b, c { ctrl @ ctrl @ x a, b, c; }"},
      {"cswap", "", "q[3], q[1], q[2]",
       "gate cswap_def a, b, c { ctrl @ swap a, b, c; }"},
      {"cu", "(0.3, 0.7, -1.1, 0.5)", "q[2], q[1]",
       "gate cu_def(t, f, l, g) a, b { p(g-t/2) a; ctrl @ U(t, f, l) a, b; }"},
      // CX kept as CNOT, not the file's ctrl @ U(π, 0, π) a, b;
      {"CX", "", "q[2], q[1]", "gate CX_def a, b { cx a, b; }"},
      {"phase", "(0.3)", "q[1]", "gate phase_def(l) a { U(0, 0, l) a; }"},
      {"cphase", "(0.3)", "q[2], q[1]",
       "gate cphase_def(l) a, b { ctrl @ phase(l) a, b; }"},
      {"id", "", "q[1]", "gate id_def a { U(0, 0, 0) a; }"},
      {"u1", "(0.3)", "q[1]", "gate u1_def(l) a { U(0, 0, l) a; }"},
      {"u2", "(0.7, -1.1)", "q[1]",
       "gate u2_def(f, l) a { gphase(-(f+l+π/2)/2); U(π/2, f, l) a; }"},
      {"u3", "(0.3, 0.7, -1.1)", "q[1]",
       "gate u3_def(t, f, l) a { gphase(-(f+l+t)/2); U(t, f, l) a; }"},
  };
  for (size_t i = 0; i < sizeof rows / sizeof *rows; i++) {
    // the row and its body, controlled by q[0]
    char row_call[128], def_call[128];
    snprintf(row_call, sizeof row_call, "ctrl @ %s%s q[0], %s;", rows[i].row,
             rows[i].params, rows[i].qubits);
    snprintf(def_call, sizeof def_call, "ctrl @ %s_def%s q[0], %s;",
             rows[i].row, rows[i].params, rows[i].qubits);
    QCS_State a = run_call(rows[i].def, row_call);
    QCS_State b = run_call(rows[i].def, def_call);

    // compare the amplitudes, phases included
    for (size_t k = 0; k < qcs_state_size(&a); k++) {
      if (!qcs_c128_near(a.amps[k], b.amps[k])) {
        fprintf(stderr, "[e] %s: amplitude %zu differs from the file's body\n",
                rows[i].row, k);
        assert(false);
      }
    }
    qcs_state_destroy(&a);
    qcs_state_destroy(&b);
  }
}

// Labels: the call's name and parameters
static void test_labels(void) {
  QCS_QasmProgram program = expect_program(STDGATES "qubit[2] q;\n"
                                                    "rz(pi / 2) q[0];\n"
                                                    "s q[0];\n"
                                                    "CX q[0], q[1];\n"
                                                    "swap q[0], q[1];");
  QCS_Gate *gates = run_of(&program)->gates;
  assert(qcs_circuit_gate_count(run_of(&program)) == 6);
  assert(strcmp(gates[0].name, "rz") == 0);
  assert(gates[0].param_count == 1 && gates[0].params[0] == QCS_PI / 2);
  assert(strcmp(gates[1].name, "s") == 0 && gates[1].param_count == 0);
  assert(strcmp(gates[2].name, "CX") == 0);
  for (int i = 3; i < 6; i++) {
    assert(strcmp(gates[i].name, "swap") == 0);
  }
  qcs_qasm_program_destroy(&program);
}

// Bell circuit on the CPU simulator; barrier emits nothing
static void test_bell(void) {
  QCS_QasmProgram program = expect_program(STDGATES "qubit[2] q;\n"
                                                    "h q[0];\n"
                                                    "barrier q;\n"
                                                    "cx q[0], q[1];\n"
                                                    "barrier;");
  assert(qcs_circuit_gate_count(run_of(&program)) == 2);
  QCS_State state;
  assert(qcs_state_create(2, &state) == QCS_ERR_OK);
  assert(qcs_circuit_run(run_of(&program), &state) == QCS_ERR_OK);
  assert(qcs_c128_near(state.amps[0], 1 / sqrt(2)));
  assert(qcs_c128_near(state.amps[1], 0));
  assert(qcs_c128_near(state.amps[2], 0));
  assert(qcs_c128_near(state.amps[3], 1 / sqrt(2)));
  qcs_state_destroy(&state);
  qcs_qasm_program_destroy(&program);

  expect_error("bit c; barrier c;", 1, 16);
}

// Modifiers on the gates a call builds, innermost first
static void test_modifiers(void) {
  const double pi = QCS_PI;
  // controls come first, outermost modifier first
  expect_gates("ctrl @ h q[2], q[0];",
               (QCS_Gate[]){with_control(qcs_gate_create_h(0), 2)}, 1);
  QCS_Gate ccx = with_control(with_control(qcs_gate_create_x(2), 0), 1);
  expect_gates("ctrl(2) @ x q[0], q[1], q[2];", &ccx, 1);
  expect_gates("ctrl @ ctrl @ x q[0], q[1], q[2];", &ccx, 1);
  expect_gates("ctrl @ swap q[2], q[0], q[1];",
               (QCS_Gate[]){with_control(qcs_gate_create_cx(1, 0), 2),
                            with_control(qcs_gate_create_cx(0, 1), 2),
                            with_control(qcs_gate_create_cx(1, 0), 2)},
               3);
  expect_gates("negctrl @ x q[1], q[0];",
               (QCS_Gate[]){with_control_zero(qcs_gate_create_x(0), 1)}, 1);
  expect_gates(
      "ctrl @ negctrl @ x q[2], q[1], q[0];",
      (QCS_Gate[]){with_control(with_control_zero(qcs_gate_create_x(0), 1), 2)},
      1);

  // inverse and powers
  expect_gates("inv @ s q[0];", (QCS_Gate[]){qcs_gate_create_p(0, -pi / 2)}, 1);
  expect_gates("inv @ rz(0.3) q[0];", (QCS_Gate[]){qcs_gate_create_rz(0, -0.3)},
               1);
  expect_gates("inv @ U(1, 2, 3) q[0];",
               (QCS_Gate[]){qcs_gate_create_u(0, -1, -3, -2)}, 1);
  expect_gates(
      "pow(2) @ s q[0];",
      (QCS_Gate[]){qcs_gate_create_p(0, pi / 2), qcs_gate_create_p(0, pi / 2)},
      2);
  expect_gates("pow(-1) @ t q[0];", (QCS_Gate[]){qcs_gate_create_p(0, -pi / 4)},
               1);
  expect_gates("pow(0) @ x q[0];", nullptr, 0);
  // inv and ctrl commute
  QCS_Gate crz = with_control(qcs_gate_create_rz(0, -0.3), 1);
  expect_gates("inv @ ctrl @ rz(0.3) q[1], q[0];", &crz, 1);
  expect_gates("ctrl @ inv @ rz(0.3) q[1], q[0];", &crz, 1);

  // global phase: a number on the circuit, under inv and pow too
  expect_phase("gphase(pi / 2);", pi / 2);
  expect_phase("inv @ gphase(0.3);", -0.3);
  expect_phase("pow(3) @ gphase(0.1);", 0.3);
  expect_phase("pow(-1) @ gphase(0.1);", -0.1);
  QCS_QasmProgram program = expect_program("gphase(0.3);");
  assert(qcs_circuit_gate_count(run_of(&program)) == 0);
  assert(qcs_f64_near(run_of(&program)->global_phase, 0.3));
  qcs_qasm_program_destroy(&program);

  // controlled: a phase on its first control, the others as controls
  expect_gates("ctrl @ gphase(0.3) q[0];",
               (QCS_Gate[]){qcs_gate_create_p(0, 0.3)}, 1);
  expect_gates("ctrl(2) @ gphase(0.3) q[0], q[1];",
               (QCS_Gate[]){with_control(qcs_gate_create_p(0, 0.3), 1)}, 1);
  QCS_Gate phase_on_zero = {.m = {{cexp(0.3 * I), 0}, {0, 1}}, .target = 0};
  expect_gates("negctrl @ gphase(0.3) q[0];", &phase_on_zero, 1);
  // the innermost ctrl adds the last control first: it's the target
  expect_gates("ctrl @ ctrl @ gphase(0.3) q[0], q[1];",
               (QCS_Gate[]){with_control(qcs_gate_create_p(1, 0.3), 0)}, 1);
  expect_gates("ctrl @ gphase(0.3) q;",
               (QCS_Gate[]){qcs_gate_create_p(0, 0.3),
                            qcs_gate_create_p(1, 0.3),
                            qcs_gate_create_p(2, 0.3)},
               3);

  // broadcast over controls too
  program = expect_program(STDGATES "qubit[2] a; qubit[2] b;\n"
                                    "ctrl @ x a, b;");
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_cx(2, 0)));
  assert(gate_is(run_of(&program)->gates[1], qcs_gate_create_cx(3, 1)));
  qcs_qasm_program_destroy(&program);

  // negctrl acts when the control is 0
  program = expect_program(STDGATES "qubit[2] q;\nnegctrl @ x q[1], q[0];");
  QCS_State state;
  assert(qcs_state_create(2, &state) == QCS_ERR_OK);
  assert(qcs_circuit_run(run_of(&program), &state) == QCS_ERR_OK);
  assert(qcs_c128_near(state.amps[1], 1));
  qcs_state_destroy(&state);
  qcs_qasm_program_destroy(&program);
}

// Labels after modifiers: inverse names, negated angles, or none
static void test_modifier_labels(void) {
  QCS_QasmProgram program = expect_program(STDGATES "qubit[2] q;\n"
                                                    "inv @ s q[0];\n"
                                                    "inv @ rz(0.3) q[0];\n"
                                                    "inv @ U(1, 2, 3) q[0];\n"
                                                    "inv @ sx q[0];\n"
                                                    "ctrl @ h q[0], q[1];\n"
                                                    "inv @ x q[0];");
  QCS_Gate *gates = run_of(&program)->gates;
  assert(strcmp(gates[0].name, "sdg") == 0 && gates[0].param_count == 0);
  assert(strcmp(gates[1].name, "rz") == 0 && gates[1].params[0] == -0.3);
  assert(strcmp(gates[2].name, "U") == 0);
  assert(gates[2].params[0] == -1 && gates[2].params[1] == -3 &&
         gates[2].params[2] == -2);
  assert(gates[3].name == nullptr);
  assert(strcmp(gates[4].name, "h") == 0);
  assert(strcmp(gates[5].name, "x") == 0);
  qcs_qasm_program_destroy(&program);
}

// Modifier errors at the argument, the name, the operand or the modifier
static void test_modifier_errors(void) {
#define Q STDGATES "qubit[3] q;\n"
  expect_error(Q "ctrl(0) @ x q[0], q[1];", 3, 6);
  expect_error(Q "ctrl(1.5) @ x q[0], q[1];", 3, 6);
  expect_error(Q "pow(0.5) @ x q[0];", 3, 5);
  expect_error(Q "ctrl @ x q[0];", 3, 8);
  expect_error(Q "ctrl @ x q[0], q[0];", 3, 16);
  expect_error(Q "pow(300000) @ x q[0];", 3, 1);
  expect_error(Q "ctrl(33) @ x q[0];", 3, 1);
  expect_error(Q "ctrl(31) @ ccx q[0], q[1], q[2];", 3, 1);
  expect_error(Q "gphase(pi) q[0];", 3, 1);
#undef Q
}

// source parses, its program fails; the error
static QCS_QasmError program_error(const char *source) {
  QCS_Arena arena = {0};
  QCS_QasmStmt *statements;
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse_program(source, &arena, &statements, &error));
  assert(!qcs_qasm_program_create(statements, nullptr, 0, &program, &error));
  qcs_arena_free(&arena);
  return error;
}

// chain of gates g0 .. g{count - 1}, each calling the one before calls
// times, then a call to the last
static void write_chain(char *out, size_t size, uint32_t count,
                        uint32_t calls) {
  size_t n =
      (size_t)snprintf(out, size, "qubit q;\ngate g0 a { U(0, 0, 0) a; }\n");
  for (uint32_t i = 1; i < count; i++) {
    n += (size_t)snprintf(out + n, size - n, "gate g%u a {", i);
    for (uint32_t c = 0; c < calls; c++) {
      n += (size_t)snprintf(out + n, size - n, " g%u a;", i - 1);
    }
    n += (size_t)snprintf(out + n, size - n, " }\n");
  }
  snprintf(out + n, size - n, "g%u q;", count - 1);
}

// Gate definitions: the body on the call's qubits, modifiers on all of it
static void test_definitions(void) {
  const double pi = QCS_PI;
  // the same gates as written out
  expect_gates("gate bell a, b { h a; cx a, b; }\nbell q[2], q[0];",
               (QCS_Gate[]){qcs_gate_create_h(2), qcs_gate_create_cx(0, 2)}, 2);
  expect_gates(
      "gate r(t) a { rz(t / 2) a; rx(t) a; }\nr(0.6) q[1];",
      (QCS_Gate[]){qcs_gate_create_rz(1, 0.3), qcs_gate_create_rx(1, 0.6)}, 2);
  // constants and gates defined before
  expect_gates("const float k = 0.5;\ngate r a { rx(k) a; }\nr q[0];",
               (QCS_Gate[]){qcs_gate_create_rx(0, 0.5)}, 1);
  expect_gates("gate g a { x a; }\ngate gg a, b { g b; g a; }\ngg q[0], q[1];",
               (QCS_Gate[]){qcs_gate_create_x(1), qcs_gate_create_x(0)}, 2);
  // broadcast
  expect_gates("gate g a { x a; }\ng q;",
               (QCS_Gate[]){qcs_gate_create_x(0), qcs_gate_create_x(1),
                            qcs_gate_create_x(2)},
               3);
  // modifiers on the whole body
  expect_gates(
      "gate sh a { s a; h a; }\ninv @ sh q[0];",
      (QCS_Gate[]){qcs_gate_create_h(0), qcs_gate_create_p(0, -pi / 2)}, 2);
  expect_gates("gate xx a, b { x a; x b; }\nctrl @ xx q[2], q[0], q[1];",
               (QCS_Gate[]){with_control(qcs_gate_create_x(0), 2),
                            with_control(qcs_gate_create_x(1), 2)},
               2);
  // a body's global phase, placed at the outermost call
  expect_gates("gate ph a { gphase(0.3); }\nctrl @ ph q[1], q[0];",
               (QCS_Gate[]){qcs_gate_create_p(1, 0.3)}, 1);
  // the phases of a body add into one
  expect_gates("gate ph a { gphase(0.1); gphase(0.2); }\nctrl @ ph q[1], q[0];",
               (QCS_Gate[]){qcs_gate_create_p(1, 0.3)}, 1);
  expect_phase("gate ph a { gphase(0.1); gphase(0.2); }\nph q[0];", 0.3);
  // an empty body under a huge pow, without looping
  expect_gates("gate e a {}\npow(4000000000000000000) @ e q[0];", nullptr, 0);
  // labels of the body's gates
  QCS_QasmProgram program =
      expect_program(STDGATES "qubit q;\ngate g(t) a { rz(t) a; }\ng(0.2) q;");
  assert(strcmp(run_of(&program)->gates[0].name, "rz") == 0);
  assert(run_of(&program)->gates[0].params[0] == 0.2);
  qcs_qasm_program_destroy(&program);
  // 32 bodies nested, 2^18 gates
  static char source[1 << 12];
  write_chain(source, sizeof source, 32, 1);
  program = expect_program(source);
  assert(qcs_circuit_gate_count(run_of(&program)) == 1);
  qcs_qasm_program_destroy(&program);
  write_chain(source, sizeof source, 19, 2);
  program = expect_program(source);
  assert(qcs_circuit_gate_count(run_of(&program)) == 1u << 18);
  qcs_qasm_program_destroy(&program);
}

// Definition errors: at the definition, or in the body with the call
static void test_definition_errors(void) {
  // no recursion, gates defined before only
  expect_error("gate g a { g a; }", 1, 12);
  expect_error("gate g a { hh a; }", 1, 12);
  // names declared once
  expect_error("include \"stdgates.inc\"; gate h a { }", 1, 30);
  expect_error("gate g a { } gate g a { }", 1, 19);
  expect_error("gate g a, a { }", 1, 11);
  expect_error("gate g(a) a { }", 1, 11);
  // bodies see their arguments and constants, not registers
  expect_error("qubit q; gate g a { U(0, 0, 0) q; }", 1, 32);
  expect_error("gate g a { U(b, 0, 0) a; }", 1, 14);
  expect_error("gate g a { ctrl @ U(0, 0, 0) a, a; }", 1, 33);
  // counts at the call
  expect_error("gate g(t) a { } qubit q; g q;", 1, 26);
  // values at the call, in the body
  QCS_QasmError error =
      program_error("gate g(t) a { U(log(t), 0, 0) a; } qubit q; g(-1) q;");
  assert(error.line == 1 && error.column == 17);
  assert(strstr(error.message, "(in call at 1:45)"));
  // 33 bodies nested, 2^19 gates
  static char source[1 << 12];
  write_chain(source, sizeof source, 33, 1);
  error = program_error(source);
  assert(strstr(error.message, "nest more than 32 deep"));
  write_chain(source, sizeof source, 20, 2);
  error = program_error(source);
  assert(strstr(error.message, "more than 262144 gates"));
}

// for loops, unrolled
static void test_loops(void) {
  // GHZ
  expect_gates("h q[0];\nfor int i in [0:1] { cx q[i], q[i + 1]; }",
               (QCS_Gate[]){qcs_gate_create_h(0), qcs_gate_create_cx(1, 0),
                            qcs_gate_create_cx(2, 1)},
               3);
  // step range, set, empty range
  expect_gates("for int i in [2:-2:0] { x q[i]; }",
               (QCS_Gate[]){qcs_gate_create_x(2), qcs_gate_create_x(0)}, 2);
  expect_gates("for int i in {1, 0} { x q[i]; }",
               (QCS_Gate[]){qcs_gate_create_x(1), qcs_gate_create_x(0)}, 2);
  expect_gates("for int i in [1:0] { x q[i]; }", nullptr, 0);
  // nested
  expect_gates("for int i in [0:1] { for int j in [0:1] { rz(i * 2 + j) q[0]; "
               "} }",
               (QCS_Gate[]){qcs_gate_create_rz(0, 0), qcs_gate_create_rz(0, 1),
                            qcs_gate_create_rz(0, 2), qcs_gate_create_rz(0, 3)},
               4);
  // the variable shadows a constant and an outer variable, until the end
  expect_gates("const int k = 5;\nfor int k in [0:1] { rz(k) q[0]; }\n"
               "rz(k) q[0];",
               (QCS_Gate[]){qcs_gate_create_rz(0, 0), qcs_gate_create_rz(0, 1),
                            qcs_gate_create_rz(0, 5)},
               3);
  expect_gates("for int i in [0:1] { for int i in {5} { rz(i) q[0]; } "
               "rz(i) q[0]; }",
               (QCS_Gate[]){qcs_gate_create_rz(0, 5), qcs_gate_create_rz(0, 0),
                            qcs_gate_create_rz(0, 5), qcs_gate_create_rz(0, 1)},
               4);
  // calling a definition
  expect_gates("gate g(t) a { rz(t) a; }\nfor int i in [1:2] { g(i) q[i]; }",
               (QCS_Gate[]){qcs_gate_create_rz(1, 1), qcs_gate_create_rz(2, 2)},
               2);
}

// Loop errors: step, variable, iterations
static void test_loop_errors(void) {
  expect_error("for int i in [0:0:1] { }", 1, 17);
  expect_error("qubit q; for int i in [0:0] { } U(i, 0, 0) q;", 1, 35);
  expect_error("for int i in [0:2000000] { }", 1, 1);
  expect_error("for int i in [0:2000] { for int j in [0:2000] { } }", 1, 25);
  expect_error(
      "for int i in [-9223372036854775807 - 1:9223372036854775807] { }", 1, 1);
}

// clbits of outcome after the measures of source
static uint64_t clbits_of(const char *source, size_t outcome) {
  QCS_QasmProgram program = expect_program(source);
  uint64_t bits = qcs_program_clbits(&program.program, outcome);
  qcs_qasm_program_destroy(&program);
  return bits;
}

// Measures: the k-th qubit into the k-th bit, the last write to a bit wins
static void test_measures(void) {
#define Q "qubit[2] q;\nbit[2] c;\n"
  // q[0] = 1, q[1] = 0
  assert(clbits_of(Q "measure q[0] -> c[1];\nmeasure q[1] -> c[0];", 0b01) ==
         0b10);
  assert(clbits_of(Q "c[1] = measure q[0];\nc[0] = measure q[1];", 0b01) ==
         0b10);
  assert(clbits_of(Q "c = measure q;", 0b01) == 0b01);
  assert(clbits_of(Q "measure q -> c;", 0b10) == 0b10);
  assert(clbits_of(Q "measure q[0] -> c[0];\nmeasure q[1] -> c[0];", 0b01) ==
         0b00);
  assert(clbits_of(Q "for int i in [0:1] { c[i] = measure q[1 - i]; }", 0b01) ==
         0b10);
  // two registers, the last declared in the high bits
  assert(clbits_of("qubit[2] q;\nbit a;\nbit b;\nmeasure q[0] -> a;\n"
                   "measure q[1] -> b;",
                   0b10) == 0b10);
  // errors at the operand
  expect_error("qubit[2] q; bit[3] c; c = measure q;", 1, 23);
  expect_error("qubit[2] q; measure q[0] -> q[1];", 1, 29);
  expect_error("qubit[2] q; bit c; measure c -> q[0];", 1, 28);
#undef Q
}

// Gates after a measure: a run after the MEASURE, on any qubit; sampled while
// diagonal on the measured one
static void test_gates_after_measure(void) {
#define Q STDGATES "qubit a;\nqubit[2] b;\nbit c;\nmeasure b[1] -> c;\n"
  QCS_QasmProgram program =
      expect_program(Q "cx b[1], b[0];\nz b[1];\nrz(0.3) b[1];\n"
                       "ctrl @ gphase(0.3) b[1];\nh b[0];\nh a;");
  assert(qcs_ops_count(program.program.ops) == 2);
  assert(program.program.ops[0].kind == QCS_OP_MEASURE);
  assert(qcs_circuit_gate_count(&program.program.ops[1].gates) == 6);
  assert(qcs_program_can_sample(&program.program));
  qcs_qasm_program_destroy(&program);

  program = expect_program(Q "h b[1];");
  assert(!qcs_program_can_sample(&program.program));
  qcs_qasm_program_destroy(&program);
#undef Q
}

// reset: a RESET per qubit, broadcast over a register
static void test_reset(void) {
  QCS_QasmProgram program =
      expect_program("qubit[3] q;\nreset q[1];\nreset q;");
  uint32_t qubits[] = {1, 0, 1, 2};
  assert(qcs_ops_count(program.program.ops) == 4);
  for (size_t i = 0; i < 4; i++) {
    assert(program.program.ops[i].kind == QCS_OP_RESET);
    assert(program.program.ops[i].qubit == qubits[i]);
  }
  qcs_qasm_program_destroy(&program);
  expect_error("bit c; reset c;", 1, 14);
}

// Inputs from the caller, hidden in gate bodies
static void test_inputs(void) {
  QCS_QasmInput theta = {"theta", 0.3};
  QCS_QasmProgram program;
  QCS_QasmError error;
  // a copy of the source, freed once parsed
  const char *text = "input angle theta;\nqubit q;\nU(theta, 0, 0) q;";
  char *source = malloc(strlen(text) + 1);
  strcpy(source, text);
  assert(qcs_qasm_parse(source, &theta, 1, &program, &error) == QCS_ERR_OK);
  free(source);
  assert(gate_is(run_of(&program)->gates[0], qcs_gate_create_u(0, 0.3, 0, 0)));
  assert(strcmp(program.qregs[0].name, "q") == 0);
  qcs_qasm_program_destroy(&program);
  // missing, at the statement
  assert(qcs_qasm_parse("qubit q;\ninput float theta;", nullptr, 0, &program,
                        &error) == QCS_ERR_QASM_PARSE_FAILURE);
  assert(error.line == 2 && error.column == 1);
  assert(program.program.ops == nullptr && program.qregs == nullptr);
  // unknown, at 0:0
  QCS_QasmInput both[] = {{"theta", 0.3}, {"thta", 0.3}};
  assert(qcs_qasm_parse("input float theta;", both, 2, &program, &error) ==
         QCS_ERR_QASM_PARSE_FAILURE);
  assert(error.line == 0 && error.column == 0);
  assert(strstr(error.message, "unknown input thta"));
  // hidden in gate bodies
  assert(qcs_qasm_parse("input float theta;\ngate g a { U(theta, 0, 0) a; }",
                        &theta, 1, &program,
                        &error) == QCS_ERR_QASM_PARSE_FAILURE);
  assert(error.line == 2 && error.column == 14);
  assert(strstr(error.message, "unknown name theta"));
  // float and angle only, finite
  QCS_QasmInput n = {"n", 1};
  assert(qcs_qasm_parse("input int n;", &n, 1, &program, &error) ==
         QCS_ERR_QASM_PARSE_FAILURE);
  QCS_QasmInput not_finite = {"theta", NAN};
  assert(qcs_qasm_parse("input float theta;", &not_finite, 1, &program,
                        &error) == QCS_ERR_QASM_PARSE_FAILURE);
  // a syntax error leaves the program empty too
  assert(qcs_qasm_parse("qubit q", nullptr, 0, &program, &error) ==
         QCS_ERR_QASM_PARSE_FAILURE);
  assert(program.program.ops == nullptr && program.qregs == nullptr &&
         program.program.qubit_count == 0);
}

enum { SHOTS = 4000 };

// count within 5σ of a binomial(SHOTS, p)
static bool near_expected(size_t count, double p) {
  return fabs((double)count - SHOTS * p) <= 5 * sqrt(SHOTS * p * (1 - p));
}

// run source SHOTS times on the CPU, count each clbits value (below 8)
static void counts_of(const char *source, size_t counts[8]) {
  static uint64_t clbits[SHOTS];
  QCS_QasmProgram program = expect_program(source);
  QCS_Random random = qcs_random_create(7);
  assert(qcs_program_run(&program.program, SHOTS, &random, clbits) ==
         QCS_ERR_OK);
  for (size_t k = 0; k < 8; k++) {
    counts[k] = 0;
  }
  for (size_t s = 0; s < SHOTS; s++) {
    assert(clbits[s] < 8);
    counts[clbits[s]]++;
  }
  qcs_qasm_program_destroy(&program);
}

// source's last op is an IF on mask and value, with one op in its body and
// no else
static void expect_if(const char *source, uint64_t mask, uint64_t value) {
  QCS_QasmProgram program = expect_program(source);
  size_t count = qcs_ops_count(program.program.ops);
  assert(count > 0);
  const QCS_Op *op = &program.program.ops[count - 1];
  if (op->kind != QCS_OP_IF || op->branch.mask != mask ||
      op->branch.value != value) {
    fprintf(stderr, "[e] %s\nnot an IF on mask %#llx, value %#llx\n", source,
            (unsigned long long)mask, (unsigned long long)value);
    assert(false);
  }
  assert(qcs_ops_count(op->branch.then_ops) == 1);
  assert(op->branch.else_ops == nullptr);
  qcs_qasm_program_destroy(&program);
}

// if: an IF op reading the clbits, its bodies as its branches
static void test_if(void) {
#define M STDGATES "qubit[3] q;\nbit[2] c;\nc = measure q[0:1];\n"
  // a bit, a negated bit, a register compared, bit k at its clbit
  expect_if(M "if (c[0]) x q[2];", 0b01, 0b01);
  expect_if(M "if (c[1] == 1) x q[2];", 0b10, 0b10);
  expect_if(M "if (!c[0]) x q[2];", 0b01, 0);
  expect_if(M "if (c == 2) x q[2];", 0b11, 0b10);
  expect_if(STDGATES "qubit q;\nbit[4] d;\nif (d[0:2:2] == 2) x q;", 0b101,
            0b100);

  // else, a reset and a measure in a body
  QCS_QasmProgram program = expect_program(
      M "if (c[0]) { reset q[0];\nc[1] = measure q[0]; } else z q[2];");
  const QCS_Op *op = &program.program.ops[2];
  assert(op->kind == QCS_OP_IF);
  assert(qcs_ops_count(op->branch.then_ops) == 2);
  assert(op->branch.then_ops[0].kind == QCS_OP_RESET);
  assert(op->branch.then_ops[1].kind == QCS_OP_MEASURE);
  assert(qcs_ops_count(op->branch.else_ops) == 1);
  assert(op->branch.else_ops[0].kind == QCS_OP_GATES);
  qcs_qasm_program_destroy(&program);

  // nested, in a loop
  program = expect_program(M "if (c[0]) if (c[1]) x q[2];");
  op = &program.program.ops[2];
  assert(op->branch.then_ops[0].kind == QCS_OP_IF);
  assert(op->branch.then_ops[0].branch.mask == 0b10);
  qcs_qasm_program_destroy(&program);
  program = expect_program(M "for int i in [0:1] if (c[i]) x q[2];");
  assert(qcs_ops_count(program.program.ops) == 4);
  assert(program.program.ops[2].branch.mask == 0b01);
  assert(program.program.ops[3].branch.mask == 0b10);
  qcs_qasm_program_destroy(&program);
#undef M

  // run: c == 1 takes the body, 0, 2 and 3 the else; r[2] says which
  size_t counts[8];
  counts_of(STDGATES "qubit[3] q;\nbit[2] c;\nbit r;\nh q[0];\nh q[1];\n"
                     "c = measure q[0:1];\n"
                     "if (c == 1) z q[2]; else x q[2];\nr = measure q[2];",
            counts);
  for (size_t c = 0; c < 4; c++) {
    size_t taken = c == 1 ? c : 0b100 | c;
    assert(near_expected(counts[taken], 0.25));
    assert(counts[taken ^ 0b100] == 0);
  }

  // a reset by hand: q is 0 after the if
  counts_of(STDGATES "qubit q;\nbit[2] c;\nh q;\nc[0] = measure q;\n"
                     "if (c[0]) x q;\nc[1] = measure q;",
            counts);
  assert(near_expected(counts[0b00], 0.5) && near_expected(counts[0b01], 0.5));
  assert(counts[0b10] == 0 && counts[0b11] == 0);

  // a bit never measured is 0
  counts_of(STDGATES "qubit q;\nbit[2] c;\nif (!c[0]) x q;\nc[1] = measure q;",
            counts);
  assert(counts[0b10] == SHOTS);

  // teleportation: q[2] ends in the prepared state, undone to |0⟩
  counts_of(STDGATES "qubit[3] q;\nbit[2] c;\nbit result;\n"
                     "ry(pi / 3) q[0];\nrz(pi / 4) q[0];\n"
                     "h q[1];\ncx q[1], q[2];\n"
                     "cx q[0], q[1];\nh q[0];\n"
                     "c[0] = measure q[0];\n"
                     "c[1] = measure q[1];\n"
                     "if (c[1]) x q[2];\nif (c[0]) z q[2];\n"
                     "rz(-pi / 4) q[2];\nry(-pi / 3) q[2];\n"
                     "result = measure q[2];",
            counts);
  for (size_t c = 0; c < 4; c++) {
    assert(near_expected(counts[c], 0.25));
    assert(counts[0b100 | c] == 0);
  }

  // errors at the if, the value, the bits
  expect_error("qubit q; bit[2] c; if (c == 4) U(0, 0, 0) q;", 1, 29);
  expect_error("qubit q; bit[2] c; if (c) U(0, 0, 0) q;", 1, 24);
  expect_error("qubit q; if (q) U(0, 0, 0) q;", 1, 14);
}

// Printed: the counts, then the ops in order, an if as a block
static void test_print(void) {
  QCS_QasmProgram program =
      expect_program(STDGATES "qubit[2] q;\nbit[2] c;\nh q[0];\n"
                              "c[0] = measure q[0];\nif (c[0]) x q[1];\n"
                              "measure q[1] -> c[1];");

  // print into a temporary file, read it back
  FILE *file = tmpfile();
  assert(file);
  qcs_qasm_program_fdump(&program, file);
  char text[256];
  rewind(file);
  size_t length = fread(text, 1, sizeof text - 1, file);
  text[length] = '\0';
  fclose(file);
  assert(strcmp(text, "qubits 2, clbits 2\n"
                      "h q[0]\n"
                      "measure q[0] -> c[0]\n"
                      "if c[0] == 1 {\n"
                      "  x q[1]\n"
                      "}\n"
                      "measure q[1] -> c[1]\n") == 0);
  qcs_qasm_program_destroy(&program);
}

int main(void) {
  test_registers();
  test_register_errors();
  test_values();
  test_value_errors();
  test_calls();
  test_call_errors();
  test_broadcast();
  test_stdgates();
  test_stdgates_file();
  test_labels();
  test_bell();
  test_modifiers();
  test_modifier_labels();
  test_modifier_errors();
  test_definitions();
  test_definition_errors();
  test_loops();
  test_loop_errors();
  test_measures();
  test_gates_after_measure();
  test_reset();
  test_inputs();
  test_if();
  test_print();
  puts("qasm program ok");
  return 0;
}
