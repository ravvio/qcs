// Checks the OpenQASM syntax tree: arena, expression nodes, dumps

#undef NDEBUG
#include "qasm/qcs_qasm_ast.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Identifier token for text, at line:column
static QCS_QasmToken ident(const char *text, uint32_t line, uint32_t column) {
  return (QCS_QasmToken){.kind = QCS_QASM_TOKEN_IDENT,
                         .start = text,
                         .length = (uint32_t)strlen(text),
                         .line = line,
                         .column = column};
}

// Dump of expr equals text
static bool dump_is(const QCS_QasmExpr *expr, const char *text) {
  char out[128];
  size_t length = qcs_qasm_expr_dump(expr, out, sizeof out);
  return length == strlen(text) && strcmp(out, text) == 0;
}

// Allocations are zeroed, aligned, kept apart, across blocks
static void test_arena(void) {
  QCS_Arena arena = {0};
  uint8_t *previous[200];
  for (int i = 0; i < 200; i++) {
    // sizes that don't divide the block size
    size_t size = 1000 + (size_t)i;
    uint8_t *p = qcs_arena_alloc(&arena, size);
    assert(p);
    assert((uintptr_t)p % alignof(max_align_t) == 0);
    for (size_t j = 0; j < size; j++) {
      assert(p[j] == 0);
    }
    memset(p, i, size);
    previous[i] = p;
  }
  // 200 KB took several 64 KiB blocks, earlier ones untouched
  for (int i = 0; i < 200; i++) {
    assert(previous[i][0] == (uint8_t)i);
    assert(previous[i][999] == (uint8_t)i);
  }

  // bigger than a block
  uint8_t *big = qcs_arena_alloc(&arena, 3 * QCS_ARENA_BLOCK_SIZE);
  assert(big);
  big[3 * QCS_ARENA_BLOCK_SIZE - 1] = 1;
  // small ones after it still work
  assert(qcs_arena_alloc(&arena, 8));

  qcs_arena_free(&arena);
  assert(arena.blocks == nullptr && arena.used == 0);
}

// Hand-built trees dump as S-expressions
static void test_dump(void) {
  QCS_Arena arena = {0};
  QCS_QasmToken at = ident("", 1, 1);

  // 1 + 2 * x
  QCS_QasmExpr *product = qcs_qasm_expr_binary(
      &arena, at, QCS_QASM_OP_MUL, qcs_qasm_expr_int(&arena, at, 2),
      qcs_qasm_expr_name(&arena, ident("x", 1, 9)));
  QCS_QasmExpr *sum = qcs_qasm_expr_binary(
      &arena, at, QCS_QASM_OP_ADD, qcs_qasm_expr_int(&arena, at, 1), product);
  assert(dump_is(sum, "(+ 1 (* 2 x))"));

  // -(2 ** 2), the unary minus outside
  QCS_QasmExpr *power = qcs_qasm_expr_binary(&arena, at, QCS_QASM_OP_POW,
                                             qcs_qasm_expr_int(&arena, at, 2),
                                             qcs_qasm_expr_int(&arena, at, 2));
  assert(dump_is(qcs_qasm_expr_unary(&arena, at, QCS_QASM_OP_NEG, power),
                 "(- (** 2 2))"));

  // sin(pi / 2), a % b, -1
  QCS_QasmExpr *half_pi =
      qcs_qasm_expr_binary(&arena, at, QCS_QASM_OP_DIV,
                           qcs_qasm_expr_name(&arena, ident("pi", 1, 5)),
                           qcs_qasm_expr_int(&arena, at, 2));
  assert(dump_is(qcs_qasm_expr_call(&arena, ident("sin", 1, 1), half_pi),
                 "(sin (/ pi 2))"));
  assert(dump_is(
      qcs_qasm_expr_binary(&arena, at, QCS_QASM_OP_MOD,
                           qcs_qasm_expr_name(&arena, ident("a", 1, 1)),
                           qcs_qasm_expr_name(&arena, ident("b", 1, 5))),
      "(% a b)"));
  assert(dump_is(qcs_qasm_expr_int(&arena, at, -1), "-1"));

  // floats always show a point or an exponent
  assert(dump_is(qcs_qasm_expr_float(&arena, at, 2.0), "2.0"));
  assert(dump_is(qcs_qasm_expr_float(&arena, at, 0.5), "0.5"));
  assert(dump_is(qcs_qasm_expr_float(&arena, at, 1e20), "1e+20"));

  // positions come from the token given
  QCS_QasmExpr *name = qcs_qasm_expr_name(&arena, ident("theta", 3, 7));
  assert(name->line == 3 && name->column == 7);

  qcs_arena_free(&arena);
}

// A short buffer gets a cut, terminated dump and the full length
static void test_dump_cut(void) {
  QCS_Arena arena = {0};
  QCS_QasmToken at = ident("", 1, 1);
  QCS_QasmExpr *sum = qcs_qasm_expr_binary(&arena, at, QCS_QASM_OP_ADD,
                                           qcs_qasm_expr_int(&arena, at, 10),
                                           qcs_qasm_expr_int(&arena, at, 20));
  char out[6];
  assert(qcs_qasm_expr_dump(sum, out, sizeof out) == strlen("(+ 10 20)"));
  assert(strcmp(out, "(+ 10") == 0);
  assert(qcs_qasm_expr_dump(sum, nullptr, 0) == strlen("(+ 10 20)"));
  qcs_arena_free(&arena);
}

int main(void) {
  test_arena();
  test_dump();
  test_dump_cut();
  puts("qasm ast ok");
  return 0;
}
