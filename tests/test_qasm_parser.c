// Checks the OpenQASM 3 expression parser: trees as dumps, errors with
// positions

#undef NDEBUG
#include "qasm/qcs_qasm_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// source parses to a tree that dumps as text
static void expect_tree(const char *source, const char *text) {
  QCS_Arena arena = {0};
  QCS_QasmExpr *expr;
  QCS_QasmError error;
  if (!qcs_qasm_parse_expression(source, &arena, &expr, &error)) {
    fprintf(stderr, "[e] %s: %u:%u: %s\n", source, error.line, error.column,
            error.message);
    assert(false);
  }
  char out[256];
  qcs_qasm_expr_dump(expr, out, sizeof out);
  if (strcmp(out, text) != 0) {
    fprintf(stderr, "[e] %s: got %s, expected %s\n", source, out, text);
    assert(false);
  }
  qcs_arena_free(&arena);
}

// source fails at line:column
static void expect_tree_error(const char *source, uint32_t line,
                              uint32_t column) {
  QCS_Arena arena = {0};
  QCS_QasmExpr *expr;
  QCS_QasmError error;
  assert(!qcs_qasm_parse_expression(source, &arena, &expr, &error));
  if (error.line != line || error.column != column) {
    fprintf(stderr, "[e] %s: error at %u:%u (%s), expected %u:%u\n", source,
            error.line, error.column, error.message, line, column);
    assert(false);
  }
  assert(strlen(error.message) > 0);
  qcs_arena_free(&arena);
}

// Precedence and associativity
static void test_precedence(void) {
  expect_tree("1 + 2 * x", "(+ 1 (* 2 x))");
  expect_tree("(1 + 2) * 3", "(* (+ 1 2) 3)");
  expect_tree("1 - 2 - 3", "(- (- 1 2) 3)");
  expect_tree("8 / 2 / 2", "(/ (/ 8 2) 2)");
  expect_tree("a % b", "(% a b)");
  // power binds tighter than unary minus and groups to the right
  expect_tree("-2 ** 2", "(- (** 2 2))");
  expect_tree("2 ** 3 ** 2", "(** 2 (** 3 2))");
  expect_tree("2 ** -1", "(** 2 (- 1))");
  expect_tree("--1", "(- (- 1))");
}

// Literals, names and functions
static void test_leaves(void) {
  expect_tree("sin(pi / 2)", "(sin (/ pi 2))");
  expect_tree("\xcf\x80 * 2", "(* \xcf\x80 2)");
  expect_tree("arccos(1)", "(arccos 1)");
  expect_tree("log(x)", "(log x)");
  expect_tree("2.0", "2.0");
  expect_tree("1e-3", "0.001");
  expect_tree("0x10", "16");
}

// Errors at the offending token, "expected" ones right after the last one
static void test_errors(void) {
  expect_tree_error("", 1, 1);
  expect_tree_error("1 +", 1, 4);
  expect_tree_error("1 + @", 1, 4);
  expect_tree_error("(1", 1, 3);
  expect_tree_error("1 2", 1, 3);
  expect_tree_error("sin pi", 1, 4);
  // operators outside the subset; '^' is xor, not power
  expect_tree_error("1 == 2", 1, 3);
  expect_tree_error("(1 && 2)", 1, 4);
  expect_tree_error("!x", 1, 1);
  expect_tree_error("2 ^ 3", 1, 3);
  // unknown functions, ln included (OpenQASM 3 has log)
  expect_tree_error("ln(1)", 1, 1);
  expect_tree_error("foo(1)", 1, 1);
  // past int64
  expect_tree_error("0xFFFFFFFFFFFFFFFF", 1, 1);
  // a lexer error
  expect_tree_error("1 + $", 1, 5);
}

// Nesting up to the limit parses, past it fails without a crash
static void test_nesting(void) {
  char source[2048];
  // 60 parentheses
  memset(source, '(', 60);
  source[60] = '1';
  memset(source + 61, ')', 60);
  source[121] = '\0';
  expect_tree(source, "1");

  // 100 parentheses
  memset(source, '(', 100);
  source[100] = '1';
  memset(source + 101, ')', 100);
  source[201] = '\0';
  QCS_Arena arena = {0};
  QCS_QasmExpr *expr;
  QCS_QasmError error;
  assert(!qcs_qasm_parse_expression(source, &arena, &expr, &error));
  assert(strcmp(error.message, "expression nested too deep") == 0);
  qcs_arena_free(&arena);

  // 1000 minus signs
  memset(source, '-', 1000);
  source[1000] = '1';
  source[1001] = '\0';
  assert(!qcs_qasm_parse_expression(source, &arena, &expr, &error));
  assert(strcmp(error.message, "expression nested too deep") == 0);
  qcs_arena_free(&arena);
}

// source parses to statements that dump as text, one per line
static void expect_program(const char *source, const char *text) {
  QCS_Arena arena = {0};
  QCS_QasmStmt *program;
  QCS_QasmError error;
  if (!qcs_qasm_parse_program(source, &arena, &program, &error)) {
    fprintf(stderr, "[e] %s\n%u:%u: %s\n", source, error.line, error.column,
            error.message);
    assert(false);
  }
  char out[1024] = "";
  size_t length = 0;
  for (QCS_QasmStmt *stmt = program; stmt; stmt = stmt->next) {
    length += qcs_qasm_stmt_dump(stmt, out + length, sizeof out - length);
    if (stmt->next) {
      out[length++] = '\n';
    }
  }
  out[length] = '\0';
  if (strcmp(out, text) != 0) {
    fprintf(stderr, "[e] %s\ngot:\n%s\nexpected:\n%s\n", source, out, text);
    assert(false);
  }
  qcs_arena_free(&arena);
}

// source fails to parse at line:column
static void expect_program_error(const char *source, uint32_t line,
                                 uint32_t column) {
  QCS_Arena arena = {0};
  QCS_QasmStmt *program;
  QCS_QasmError error;
  assert(!qcs_qasm_parse_program(source, &arena, &program, &error));
  if (error.line != line || error.column != column) {
    fprintf(stderr, "[e] %s: error at %u:%u (%s), expected %u:%u\n", source,
            error.line, error.column, error.message, line, column);
    assert(false);
  }
  assert(strlen(error.message) > 0);
  qcs_arena_free(&arena);
}

// Header, include, declarations in both spellings
static void test_declarations(void) {
  expect_program("OPENQASM 3.0;\n"
                 "include \"stdgates.inc\";\n"
                 "qubit[2] q;\n"
                 "qubit r;\n"
                 "bit[2] c;\n"
                 "qreg a[3];\n"
                 "creg b[1];",
                 "(include \"stdgates.inc\")\n"
                 "(qubit q 2)\n"
                 "(qubit r)\n"
                 "(bit c 2)\n"
                 "(qubit a 3)\n"
                 "(bit b 1)");
  // the header is optional, 3 without minor works, comments anywhere
  expect_program("qubit q;", "(qubit q)");
  expect_program("OPENQASM 3; qubit q;", "(qubit q)");
  expect_program("/* a */ OPENQASM 3; // b\nh /* c */ q;", "(call h () (q))");
  expect_program("", "");
}

// const and input, widths ignored
static void test_constants(void) {
  expect_program("const int n = 4;\n"
                 "const float t = pi / 2;\n"
                 "input angle theta;\n"
                 "const int[32] m = n * 2;",
                 "(const int n 4)\n"
                 "(const float t (/ pi 2))\n"
                 "(input angle theta)\n"
                 "(const int m (* n 2))");
}

// Gate calls: parameters, operands, slices, modifiers
static void test_calls(void) {
  expect_program("h q[0];\n"
                 "cx q[0], q[1];\n"
                 "rz(pi / 2) q;\n"
                 "U(1, 2, 3) q[0];\n"
                 "gphase(pi);\n"
                 "h q[0:2];\n"
                 "h q[0:2:4];\n"
                 "foo() q;",
                 "(call h () (q[0]))\n"
                 "(call cx () (q[0] q[1]))\n"
                 "(call rz ((/ pi 2)) (q))\n"
                 "(call U (1 2 3) (q[0]))\n"
                 "(call gphase (pi) ())\n"
                 "(call h () (q[0:2]))\n"
                 "(call h () (q[0:2:4]))\n"
                 "(call foo () (q))");
  expect_program("ctrl @ h a, b;\n"
                 "ctrl(2) @ x a, b, c;\n"
                 "negctrl @ inv @ pow(2) @ s a, b;",
                 "(call h () (a b) (ctrl))\n"
                 "(call x () (a b c) (ctrl 2))\n"
                 "(call s () (a b) (negctrl) (inv) (pow 2))");
}

// The three measurement forms, reset and barrier
static void test_measure(void) {
  expect_program("c[0] = measure q[0];\n"
                 "c = measure q;\n"
                 "measure q[1] -> c[1];\n"
                 "reset q[0];\n"
                 "reset q;\n"
                 "barrier q;\n"
                 "barrier;",
                 "(measure (q[0]) (c[0]))\n"
                 "(measure (q) (c))\n"
                 "(measure (q[1]) (c[1]))\n"
                 "(reset (q[0]))\n"
                 "(reset (q))\n"
                 "(barrier (q))\n"
                 "(barrier ())");
}

// Gate definitions and loops with their bodies
static void test_blocks(void) {
  expect_program("gate g(t) a, b { rz(t / 2) a; cx a, b; barrier a; }\n"
                 "gate e a { }\n"
                 "gate k() a { ctrl @ h a; }",
                 "(gate g (t) (a b) (call rz ((/ t 2)) (a)) "
                 "(call cx () (a b)) (barrier (a)))\n"
                 "(gate e () (a))\n"
                 "(gate k () (a) (call h () (a) (ctrl)))");
  expect_program("for int i in [0:2] { h q[i]; }\n"
                 "for uint i in [0:2:6] h q[i];\n"
                 "for i in {1, 3} { c[i] = measure q[i]; }\n"
                 "for int i in [0:1] for int j in [0:1] cx q[i], q[j];",
                 "(for int i [0:2] (call h () (q[i])))\n"
                 "(for uint i [0:2:6] (call h () (q[i])))\n"
                 "(for int i {1 3} (measure (q[i]) (c[i])))\n"
                 "(for int i [0:1] (for int j [0:1] (call cx () (q[i] "
                 "q[j]))))");
  expect_program("if (c[0]) x q;\n"
                 "if (!c[1]) { x q; h q; }\n"
                 "if (c == 2) x q; else h q;\n"
                 "if (c[0]) if (c[1]) x q;\n"
                 "for i in [0:1] if (c[i]) x q[i];\n"
                 "if (c[0]) { reset q; c[1] = measure q; }",
                 "(if (c[0]) (call x () (q)))\n"
                 "(if (! (c[1])) (call x () (q)) (call h () (q)))\n"
                 "(if (== (c) 2) (call x () (q)) (else (call h () (q))))\n"
                 "(if (c[0]) (if (c[1]) (call x () (q))))\n"
                 "(for int i [0:1] (if (c[i]) (call x () (q[i]))))\n"
                 "(if (c[0]) (reset (q)) (measure (q) (c[1])))");
}

// Statement errors at the offending token, "expected" ones right after the
// last one
static void test_statement_errors(void) {
  // header and include
  expect_program_error("OPENQASM 2.0;", 1, 10);
  expect_program_error("OPENQASM;", 1, 9);
  expect_program_error("OPENQASM 3.0", 1, 13);
  expect_program_error("h q;\nOPENQASM 3;", 2, 1);
  expect_program_error("include \"qelib1.inc\";", 1, 9);
  // declarations and variables
  expect_program_error("qubit[2];", 1, 9);
  expect_program_error("qubit q", 1, 8);
  expect_program_error("bit[2] c = \"00\";", 1, 10);
  expect_program_error("int x = 1;", 1, 1);
  expect_program_error("const n = 4;", 1, 6);
  // outside the subset
  expect_program_error("while (c == 1) x q;", 1, 1);
  expect_program_error("@x h q;", 1, 1);
  expect_program_error("3 q;", 1, 1);
  // gate calls
  expect_program_error("h q[0]", 1, 7);
  expect_program_error("h q[0] q[1];", 1, 7);
  expect_program_error("rz(1 2) q;", 1, 5);
  expect_program_error("pow @ x q;", 1, 4);
  expect_program_error("inv(2) @ x q;", 1, 4);
  expect_program_error("ctrl x q;", 1, 5);
  // measurement
  expect_program_error("c = 1;", 1, 5);
  expect_program_error("measure q c;", 1, 10);
  expect_program_error("reset q, r;", 1, 8);
  // gate definitions
  expect_program_error("gate g a { h a[0]; }", 1, 15);
  expect_program_error("gate g a { measure a -> c; }", 1, 12);
  expect_program_error("gate g a { reset a; }", 1, 12);
  expect_program_error("gate g a { gate f b { } }", 1, 12);
  expect_program_error("gate g a { qubit r; }", 1, 12);
  expect_program_error("gate g { h a; }", 1, 7);
  expect_program_error("gate g a { h a;", 1, 16);
  // loops
  expect_program_error("for int i in [0:2] { qubit r; }", 1, 22);
  expect_program_error("for float x in [0:1] h q;", 1, 5);
  expect_program_error("for int i [0:1] h q;", 1, 10);
  expect_program_error("for int i in q h q;", 1, 13);
  expect_program_error("for int i in [0] h q;", 1, 16);
  // if
  expect_program_error("else x q;", 1, 1);
  expect_program_error("if c[0] x q;", 1, 3);
  expect_program_error("if (c[0] != 1) x q;", 1, 9);
  expect_program_error("if (!c == 1) x q;", 1, 7);
  expect_program_error("if (c[0]) for i in [0:1] x q;", 1, 11);
  expect_program_error("gate g a { if (c[0]) x a; }", 1, 12);
}

// Loops nest up to the limit, past it fail without a crash
static void test_loop_nesting(void) {
  char source[2048] = "";
  for (int i = 0; i < 20; i++) {
    strcat(source, "for i in [0:1] ");
  }
  strcat(source, "h q;");
  QCS_Arena arena = {0};
  QCS_QasmStmt *program;
  QCS_QasmError error;
  assert(qcs_qasm_parse_program(source, &arena, &program, &error));
  qcs_arena_free(&arena);

  source[0] = '\0';
  for (int i = 0; i < 40; i++) {
    strcat(source, "for i in [0:1] ");
  }
  strcat(source, "h q;");
  assert(!qcs_qasm_parse_program(source, &arena, &program, &error));
  assert(strcmp(error.message, "blocks nested too deep") == 0);
  qcs_arena_free(&arena);
}

int main(void) {
  test_precedence();
  test_leaves();
  test_errors();
  test_nesting();
  test_declarations();
  test_constants();
  test_calls();
  test_measure();
  test_blocks();
  test_statement_errors();
  test_loop_nesting();
  puts("qasm parser ok");
  return 0;
}
