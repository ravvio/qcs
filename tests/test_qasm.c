// Checks the OpenQASM 2 parser: program built, errors with positions

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// Header line, so statements start on line 2
#define HEADER "OPENQASM 2.0;\n"

// Parsing source succeeds
static QCS_QasmProgram expect_ok(const char *source) {
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse(source, &program, &error) == QCS_ERR_OK);
  return program;
}

// Parsing source fails at line:column
static void expect_error(const char *source, uint32_t line, uint32_t column) {
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse(source, &program, &error) ==
         QCS_ERR_QASM_PARSE_FAILURE);
  assert(error.line == line);
  assert(error.column == column);
  assert(strlen(error.message) > 0);
}

// Header alone: an empty program
static void test_header(void) {
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse("OPENQASM 2.0;", &program, &error) == QCS_ERR_OK);
  assert(program.circuit.qubit_count == 0);
  assert(qcs_circuit_gate_count(&program.circuit) == 0);
  assert(program.qregs == nullptr);
  assert(program.cregs == nullptr);
  assert(program.measures == nullptr);
  assert(program.clbit_count == 0);
  qcs_qasm_program_destroy(&program);

  // comments and line breaks between tokens
  assert(qcs_qasm_parse("// c\nOPENQASM\n2.0 // v\n;", &program, &error) ==
         QCS_ERR_OK);
  qcs_qasm_program_destroy(&program);
}

// Header errors, each at its line and column
static void test_header_errors(void) {
  // no header
  expect_error("", 1, 1);
  expect_error("qreg q[1];", 1, 1);
  expect_error("openqasm 2.0;", 1, 1);
  // version
  expect_error("OPENQASM 3.0;", 1, 10);
  expect_error("OPENQASM 2;", 1, 9);
  expect_error("OPENQASM;", 1, 9);
  // missing ';', right after the version
  expect_error("OPENQASM 2.0", 1, 13);
  expect_error("OPENQASM 2.0\nqreg q[1];", 1, 13);

  // a lexer error with the lexer's message
  QCS_QasmProgram program;
  QCS_QasmError error;
  assert(qcs_qasm_parse("OPENQASM @", &program, &error) ==
         QCS_ERR_QASM_PARSE_FAILURE);
  assert(error.line == 1 && error.column == 10);
  assert(strcmp(error.message, "unexpected character") == 0);
}

// include "qelib1.inc"; and its errors
static void test_include(void) {
  QCS_QasmProgram program = expect_ok(HEADER "include \"qelib1.inc\";");
  qcs_qasm_program_destroy(&program);

  // other files, at the opening quote
  expect_error(HEADER "include \"other.inc\";", 2, 9);
  expect_error(HEADER "include \"qelib1\";", 2, 9);
  expect_error(HEADER "include \"\";", 2, 9);
  // no file, right after include
  expect_error(HEADER "include;", 2, 8);
  // missing ';', right after the closing quote
  expect_error(HEADER "include \"qelib1.inc\"", 2, 21);
}

// qreg and creg: offsets in the global range, counts
static void test_registers(void) {
  QCS_QasmProgram program =
      expect_ok(HEADER "qreg a[2];\nqreg b[3];\ncreg c[4];\ncreg d[1];");
  assert(program.circuit.qubit_count == 5);
  assert(program.clbit_count == 5);
  assert(strcmp(program.qregs[0].name, "a") == 0);
  assert(program.qregs[0].offset == 0 && program.qregs[0].size == 2);
  assert(strcmp(program.qregs[1].name, "b") == 0);
  assert(program.qregs[1].offset == 2 && program.qregs[1].size == 3);
  // clbits count apart from qubits
  assert(strcmp(program.cregs[0].name, "c") == 0);
  assert(program.cregs[0].offset == 0 && program.cregs[0].size == 4);
  assert(strcmp(program.cregs[1].name, "d") == 0);
  assert(program.cregs[1].offset == 4 && program.cregs[1].size == 1);
  qcs_qasm_program_destroy(&program);

  // the maximum, in one register and over two
  program = expect_ok(HEADER "qreg q[32];");
  assert(program.circuit.qubit_count == 32);
  qcs_qasm_program_destroy(&program);
  program = expect_ok(HEADER "qreg a[20];\nqreg b[12];\ncreg c[32];");
  assert(program.circuit.qubit_count == 32);
  assert(program.clbit_count == 32);
  qcs_qasm_program_destroy(&program);
}

// Register names up to 31 characters
static void test_register_name_length(void) {
  const char *a = "aaaaaaaaaa"
                  "aaaaaaaaaa"
                  "aaaaaaaaaa"
                  "aaaaaaaaaa";
  char source[128];
  snprintf(source, sizeof source, HEADER "qreg %.*s[1];", 31, a);
  QCS_QasmProgram program = expect_ok(source);
  assert(strlen(program.qregs[0].name) == 31);
  qcs_qasm_program_destroy(&program);

  snprintf(source, sizeof source, HEADER "qreg %.*s[1];", 32, a);
  expect_error(source, 2, 6);
}

// Register errors, each at its line and column
static void test_register_errors(void) {
  // syntax
  expect_error(HEADER "qreg [2];", 2, 5);
  expect_error(HEADER "qreg q 2;", 2, 7);
  expect_error(HEADER "qreg q[2.0];", 2, 8);
  expect_error(HEADER "qreg q[];", 2, 8);
  expect_error(HEADER "qreg q[2;", 2, 9);
  expect_error(HEADER "qreg q[2]", 2, 10);
  expect_error(HEADER "creg c[2]\nqreg q[1];", 2, 10);
  // size, at the size
  expect_error(HEADER "qreg q[0];", 2, 8);
  expect_error(HEADER "creg c[0];", 2, 8);
  expect_error(HEADER "qreg q[33];", 2, 8);
  expect_error(HEADER "qreg a[20];\nqreg b[13];", 3, 8);
  expect_error(HEADER "creg a[30];\ncreg b[3];", 3, 8);
  // names used twice, in either kind, at the second name
  expect_error(HEADER "qreg q[2];\nqreg q[2];", 3, 6);
  expect_error(HEADER "qreg q[2];\ncreg q[2];", 3, 6);
  expect_error(HEADER "creg c[2];\nqreg c[2];", 3, 6);
}

// Same matrix, target and controls
static bool gate_is(QCS_Gate a, QCS_Gate b) {
  return qcs_gate_matrix_equal(a, b) && a.target == b.target &&
         a.controls == b.controls;
}

// Each gate of the table builds its phase 1 gate, control first in QASM
static void test_gate_table(void) {
  struct {
    const char *call;
    QCS_Gate expected;
  } cases[] = {
      {"id q[2];", qcs_gate_create_id(2)},
      {"x q[1];", qcs_gate_create_x(1)},
      {"y q[0];", qcs_gate_create_y(0)},
      {"z q[2];", qcs_gate_create_z(2)},
      {"h q[1];", qcs_gate_create_h(1)},
      {"cx q[2], q[0];", qcs_gate_create_cx(0, 2)},
      {"cy q[0], q[1];", qcs_gate_create_cy(1, 0)},
      {"cz q[1], q[2];", qcs_gate_create_cz(2, 1)},
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    char source[64];
    snprintf(source, sizeof source, HEADER "qreg q[3];\n%s", cases[i].call);
    QCS_QasmProgram program = expect_ok(source);
    assert(qcs_circuit_gate_count(&program.circuit) == 1);
    assert(gate_is(program.circuit.gates[0], cases[i].expected));
    qcs_qasm_program_destroy(&program);
  }
}

// Arguments in later registers map to global qubits
static void test_gate_registers(void) {
  QCS_QasmProgram program = expect_ok(HEADER "qreg a[2];\nqreg b[3];\n"
                                             "creg c[4];\n"
                                             "x b[0];\n"
                                             "cx a[1], b[2];");
  assert(qcs_circuit_gate_count(&program.circuit) == 2);
  assert(gate_is(program.circuit.gates[0], qcs_gate_create_x(2)));
  QCS_Gate cx = program.circuit.gates[1];
  assert(cx.target == 4);
  assert(cx.controls == 1u << 1);
  qcs_qasm_program_destroy(&program);

  // comments and line breaks between tokens
  program = expect_ok(HEADER "qreg q[2];\ncx // c\nq\n[\n1\n]\n,q[0]\n;");
  assert(gate_is(program.circuit.gates[0], qcs_gate_create_cx(0, 1)));
  qcs_qasm_program_destroy(&program);
}

// Bell circuit on the CPU simulator: |00⟩ and |11⟩ at 1/√2
static void test_bell(void) {
  QCS_QasmProgram program = expect_ok(HEADER "include \"qelib1.inc\";\n"
                                             "qreg q[2];\n"
                                             "h q[0];\n"
                                             "cx q[0], q[1];\n");
  QCS_State state;
  assert(qcs_state_create(program.circuit.qubit_count, &state) == QCS_ERR_OK);
  assert(qcs_circuit_run(&program.circuit, &state) == QCS_ERR_OK);
  assert(qcs_c128_near(state.amps[0], 1 / sqrt(2)));
  assert(qcs_c128_near(state.amps[1], 0));
  assert(qcs_c128_near(state.amps[2], 0));
  assert(qcs_c128_near(state.amps[3], 1 / sqrt(2)));
  qcs_state_destroy(&state);
  qcs_qasm_program_destroy(&program);
}

// Gate call errors, each at its line and column
static void test_gate_errors(void) {
#define REGS HEADER "qreg q[2]; creg c[2];\n"
  // name
  expect_error(REGS "foo q[0];", 3, 1);
  expect_error(REGS "H q[0];", 3, 1);
  expect_error(REGS "3 q[0];", 3, 1);
  // arguments
  expect_error(REGS "h r[0];", 3, 3);
  expect_error(REGS "h c[0];", 3, 3);
  expect_error(REGS "h q[2];", 3, 5);
  expect_error(REGS "h;", 3, 2);
  expect_error(REGS "h q;", 3, 4);
  expect_error(REGS "h q[0],;", 3, 8);
  // count, too few at the name, too many at the extra argument
  expect_error(REGS "cx q[0];", 3, 1);
  expect_error(REGS "h q[0], q[1];", 3, 9);
  // same qubit twice, at the second
  expect_error(REGS "cx q[0], q[0];", 3, 10);
  // missing ';', right after the ']'
  expect_error(REGS "h q[0]", 3, 7);
  expect_error(REGS "h q[0] x q[1];", 3, 7);
  expect_error(REGS "h q[0]\nx q[1];", 3, 7);
  // unsupported statements
  expect_error(REGS "gate g a { h a; }", 3, 1);
  expect_error(REGS "opaque g a;", 3, 1);
  expect_error(REGS "reset q[0];", 3, 1);
  expect_error(REGS "if (c == 1) x q[0];", 3, 1);
#undef REGS
}

// measure: global qubit -> global clbit, in order, no gates
static void test_measure(void) {
  QCS_QasmProgram program = expect_ok(HEADER "qreg q[2];\n"
                                             "creg c[2];\n"
                                             "h q[0];\n"
                                             "cx q[0], q[1];\n"
                                             "measure q[0] -> c[0];\n"
                                             "measure q[1] -> c[1];\n");
  assert(qcs_circuit_gate_count(&program.circuit) == 2);
  assert(program.measures[0].qubit == 0 && program.measures[0].clbit == 0);
  assert(program.measures[1].qubit == 1 && program.measures[1].clbit == 1);
  qcs_qasm_program_destroy(&program);

  // later registers, global indices on both sides
  program = expect_ok(HEADER "qreg a[1]; qreg b[2];\n"
                             "creg c[1]; creg d[3];\n"
                             "measure b[1] -> d[2];\n"
                             "measure a[0] -> c[0];\n");
  assert(qcs_circuit_gate_count(&program.circuit) == 0);
  assert(program.measures[0].qubit == 2 && program.measures[0].clbit == 3);
  assert(program.measures[1].qubit == 0 && program.measures[1].clbit == 0);
  qcs_qasm_program_destroy(&program);

  // comments and line breaks between tokens
  program = expect_ok(HEADER "qreg q[1]; creg c[1];\n"
                             "measure\nq[0] // c\n->\nc[0]\n;");
  assert(program.measures[0].qubit == 0 && program.measures[0].clbit == 0);
  qcs_qasm_program_destroy(&program);
}

// measure errors, each at its line and column
static void test_measure_errors(void) {
#define REGS HEADER "qreg q[2]; creg c[2];\n"
  // register kinds
  expect_error(REGS "measure c[0] -> c[1];", 3, 9);
  expect_error(REGS "measure q[0] -> q[1];", 3, 17);
  // indices
  expect_error(REGS "measure q[2] -> c[0];", 3, 11);
  expect_error(REGS "measure q[0] -> c[2];", 3, 19);
  // syntax, right after the last good token
  expect_error(REGS "measure;", 3, 8);
  expect_error(REGS "measure q[0] c[0];", 3, 13);
  expect_error(REGS "measure q[0] - > c[0];", 3, 13);
  expect_error(REGS "measure q[0] -> ;", 3, 16);
  expect_error(REGS "measure q[0] -> c[0]", 3, 21);
  // whole registers, until broadcasting
  expect_error(REGS "measure q -> c;", 3, 10);
#undef REGS
}

// barrier: arguments checked, nothing emitted
static void test_barrier(void) {
  QCS_QasmProgram program = expect_ok(HEADER "qreg a[1]; qreg b[2];\n"
                                             "h a[0];\n"
                                             "barrier a[0];\n"
                                             "barrier a[0], b[0], b[1];\n"
                                             "barrier b[1], b[1];\n"
                                             "x b[1];\n");
  assert(qcs_circuit_gate_count(&program.circuit) == 2);
  assert(gate_is(program.circuit.gates[0], qcs_gate_create_h(0)));
  assert(gate_is(program.circuit.gates[1], qcs_gate_create_x(2)));
  assert(program.measures == nullptr);
  qcs_qasm_program_destroy(&program);
}

// barrier errors, each at its line and column
static void test_barrier_errors(void) {
#define REGS HEADER "qreg q[2]; creg c[2];\n"
  // arguments
  expect_error(REGS "barrier;", 3, 8);
  expect_error(REGS "barrier c[0];", 3, 9);
  expect_error(REGS "barrier r[0];", 3, 9);
  expect_error(REGS "barrier q[2];", 3, 11);
  expect_error(REGS "barrier q[0], c[0];", 3, 15);
  expect_error(REGS "barrier q[0],;", 3, 14);
  // missing ';', right after the ']'
  expect_error(REGS "barrier q[0]", 3, 13);
  expect_error(REGS "barrier q[0] q[1];", 3, 13);
  // whole registers, until broadcasting
  expect_error(REGS "barrier q;", 3, 10);
#undef REGS
}

int main(void) {
  test_header();
  test_header_errors();
  test_include();
  test_registers();
  test_register_name_length();
  test_register_errors();
  test_gate_table();
  test_gate_registers();
  test_bell();
  test_gate_errors();
  test_measure();
  test_measure_errors();
  test_barrier();
  test_barrier_errors();
  puts("qasm ok");
  return 0;
}
