#include "qcs_qasm_ast.h"

#include <stb_ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *qcs_arena_alloc(QCS_Arena *arena, size_t size) {
  // round up to the strictest alignment
  const size_t align = alignof(max_align_t);
  size_t offset = (arena->used + align - 1) / align * align;
  if (arrlen(arena->blocks) == 0 || offset + size > arena->capacity) {
    // new block, bigger than usual for a big request
    size_t capacity = size > QCS_ARENA_BLOCK_SIZE ? size : QCS_ARENA_BLOCK_SIZE;
    uint8_t *block = malloc(capacity);
    if (!block) {
      return nullptr;
    }
    arrput(arena->blocks, block);
    arena->capacity = capacity;
    offset = 0;
  }
  arena->used = offset + size;
  void *p = arena->blocks[arrlen(arena->blocks) - 1] + offset;
  memset(p, 0, size);
  return p;
}

void qcs_arena_free(QCS_Arena *arena) {
  for (ptrdiff_t i = 0; i < arrlen(arena->blocks); i++) {
    free(arena->blocks[i]);
  }
  arrfree(arena->blocks);
  *arena = (QCS_Arena){0};
}

// node of kind at the token's position
static QCS_QasmExpr *qcs_qasm_expr_new(QCS_Arena *arena, QCS_QasmExprKind kind,
                                       QCS_QasmToken at) {
  QCS_QasmExpr *expr = qcs_arena_alloc(arena, sizeof *expr);
  if (expr) {
    expr->kind = kind;
    expr->line = at.line;
    expr->column = at.column;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_int(QCS_Arena *arena, QCS_QasmToken at,
                                int64_t value) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_INT, at);
  if (expr) {
    expr->integer = value;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_float(QCS_Arena *arena, QCS_QasmToken at,
                                  double value) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_FLOAT, at);
  if (expr) {
    expr->number = value;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_name(QCS_Arena *arena, QCS_QasmToken name) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_NAME, name);
  if (expr) {
    expr->name = name;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_unary(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmOp op, QCS_QasmExpr *operand) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_UNARY, at);
  if (expr) {
    expr->unary.op = op;
    expr->unary.operand = operand;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_binary(QCS_Arena *arena, QCS_QasmToken at,
                                   QCS_QasmOp op, QCS_QasmExpr *left,
                                   QCS_QasmExpr *right) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_BINARY, at);
  if (expr) {
    expr->binary.op = op;
    expr->binary.left = left;
    expr->binary.right = right;
  }
  return expr;
}

QCS_QasmExpr *qcs_qasm_expr_call(QCS_Arena *arena, QCS_QasmToken name,
                                 QCS_QasmExpr *argument) {
  QCS_QasmExpr *expr = qcs_qasm_expr_new(arena, QCS_QASM_EXPR_CALL, name);
  if (expr) {
    expr->call.name = name;
    expr->call.argument = argument;
  }
  return expr;
}

// Text written so far, cut at size
typedef struct {
  char *out;
  size_t size;
  size_t length; // full length, also past size
} QCS_QasmDump;

static void qcs_qasm_dump_append(QCS_QasmDump *dump, const char *format, ...) {
  size_t offset = dump->length < dump->size ? dump->length : dump->size;
  va_list args;
  va_start(args, format);
  int n = vsnprintf(dump->size ? dump->out + offset : nullptr,
                    dump->size - offset, format, args);
  va_end(args);
  dump->length += (size_t)n;
}

static const char *qcs_qasm_op_symbol(QCS_QasmOp op) {
  switch (op) {
  case QCS_QASM_OP_ADD:
    return "+";
  case QCS_QASM_OP_SUB:
  case QCS_QASM_OP_NEG:
    return "-";
  case QCS_QASM_OP_MUL:
    return "*";
  case QCS_QASM_OP_DIV:
    return "/";
  case QCS_QASM_OP_MOD:
    return "%";
  case QCS_QASM_OP_POW:
    return "**";
  }
  return "?";
}

static void qcs_qasm_dump_expr(QCS_QasmDump *dump, const QCS_QasmExpr *expr) {
  switch (expr->kind) {
  case QCS_QASM_EXPR_INT:
    qcs_qasm_dump_append(dump, "%lld", (long long)expr->integer);
    break;
  case QCS_QASM_EXPR_FLOAT: {
    // a point or exponent always, so 2.0 doesn't read as the integer 2
    char text[32];
    snprintf(text, sizeof text, "%g", expr->number);
    qcs_qasm_dump_append(dump, strpbrk(text, ".eni") ? "%s" : "%s.0", text);
    break;
  }
  case QCS_QASM_EXPR_NAME:
    qcs_qasm_dump_append(dump, "%.*s", (int)expr->name.length,
                         expr->name.start);
    break;
  case QCS_QASM_EXPR_UNARY:
    qcs_qasm_dump_append(dump, "(%s ", qcs_qasm_op_symbol(expr->unary.op));
    qcs_qasm_dump_expr(dump, expr->unary.operand);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_EXPR_BINARY:
    qcs_qasm_dump_append(dump, "(%s ", qcs_qasm_op_symbol(expr->binary.op));
    qcs_qasm_dump_expr(dump, expr->binary.left);
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_expr(dump, expr->binary.right);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_EXPR_CALL:
    qcs_qasm_dump_append(dump, "(%.*s ", (int)expr->call.name.length,
                         expr->call.name.start);
    qcs_qasm_dump_expr(dump, expr->call.argument);
    qcs_qasm_dump_append(dump, ")");
    break;
  }
}

size_t qcs_qasm_expr_dump(const QCS_QasmExpr *expr, char *out, size_t size) {
  QCS_QasmDump dump = {.out = out, .size = size};
  if (size > 0) {
    out[0] = '\0';
  }
  qcs_qasm_dump_expr(&dump, expr);
  return dump.length;
}

QCS_QasmName *qcs_qasm_name(QCS_Arena *arena, QCS_QasmToken token) {
  QCS_QasmName *name = qcs_arena_alloc(arena, sizeof *name);
  if (name) {
    name->token = token;
  }
  return name;
}

// operand of kind for the name
static QCS_QasmOperand *qcs_qasm_operand_new(QCS_Arena *arena,
                                             QCS_QasmIndexKind kind,
                                             QCS_QasmToken name) {
  QCS_QasmOperand *operand = qcs_arena_alloc(arena, sizeof *operand);
  if (operand) {
    operand->kind = kind;
    operand->name = name;
  }
  return operand;
}

QCS_QasmOperand *qcs_qasm_operand(QCS_Arena *arena, QCS_QasmToken name) {
  return qcs_qasm_operand_new(arena, QCS_QASM_INDEX_NONE, name);
}

QCS_QasmOperand *qcs_qasm_operand_index(QCS_Arena *arena, QCS_QasmToken name,
                                        QCS_QasmExpr *index) {
  QCS_QasmOperand *operand =
      qcs_qasm_operand_new(arena, QCS_QASM_INDEX_ONE, name);
  if (operand) {
    operand->index = index;
  }
  return operand;
}

QCS_QasmOperand *qcs_qasm_operand_range(QCS_Arena *arena, QCS_QasmToken name,
                                        QCS_QasmRange range) {
  QCS_QasmOperand *operand =
      qcs_qasm_operand_new(arena, QCS_QASM_INDEX_RANGE, name);
  if (operand) {
    operand->range = range;
  }
  return operand;
}

QCS_QasmModifier *qcs_qasm_modifier(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmModifierKind kind,
                                    QCS_QasmExpr *argument) {
  QCS_QasmModifier *modifier = qcs_arena_alloc(arena, sizeof *modifier);
  if (modifier) {
    modifier->kind = kind;
    modifier->line = at.line;
    modifier->column = at.column;
    modifier->argument = argument;
  }
  return modifier;
}

// statement of kind at the token's position
static QCS_QasmStmt *qcs_qasm_stmt_new(QCS_Arena *arena, QCS_QasmStmtKind kind,
                                       QCS_QasmToken at) {
  QCS_QasmStmt *stmt = qcs_arena_alloc(arena, sizeof *stmt);
  if (stmt) {
    stmt->kind = kind;
    stmt->line = at.line;
    stmt->column = at.column;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_include(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmToken file) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_INCLUDE, at);
  if (stmt) {
    stmt->include = file;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_declare(QCS_Arena *arena, QCS_QasmToken at,
                                    bool quantum, QCS_QasmToken name,
                                    QCS_QasmExpr *size) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_DECLARE, at);
  if (stmt) {
    stmt->declare.quantum = quantum;
    stmt->declare.name = name;
    stmt->declare.size = size;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_const(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmType type, QCS_QasmToken name,
                                  QCS_QasmExpr *value) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_CONST, at);
  if (stmt) {
    stmt->constant.type = type;
    stmt->constant.name = name;
    stmt->constant.value = value;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_input(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmType type, QCS_QasmToken name) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_INPUT, at);
  if (stmt) {
    stmt->input.type = type;
    stmt->input.name = name;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_gate_call(QCS_Arena *arena, QCS_QasmToken at,
                                      QCS_QasmModifier *modifiers,
                                      QCS_QasmToken name, QCS_QasmExpr *params,
                                      QCS_QasmOperand *operands) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_GATE_CALL, at);
  if (stmt) {
    stmt->call.modifiers = modifiers;
    stmt->call.name = name;
    stmt->call.params = params;
    stmt->call.operands = operands;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_gate_def(QCS_Arena *arena, QCS_QasmToken at,
                                     QCS_QasmToken name, QCS_QasmName *params,
                                     QCS_QasmName *args, QCS_QasmStmt *body) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_GATE_DEF, at);
  if (stmt) {
    stmt->gate.name = name;
    stmt->gate.params = params;
    stmt->gate.args = args;
    stmt->gate.body = body;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_for_range(QCS_Arena *arena, QCS_QasmToken at,
                                      QCS_QasmType type, QCS_QasmToken variable,
                                      QCS_QasmRange range, QCS_QasmStmt *body) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_FOR_RANGE, at);
  if (stmt) {
    stmt->loop.type = type;
    stmt->loop.variable = variable;
    stmt->loop.range = range;
    stmt->loop.body = body;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_for_set(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmType type, QCS_QasmToken variable,
                                    QCS_QasmExpr *set, QCS_QasmStmt *body) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_FOR_SET, at);
  if (stmt) {
    stmt->loop.type = type;
    stmt->loop.variable = variable;
    stmt->loop.set = set;
    stmt->loop.body = body;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_measure(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmOperand *qubit,
                                    QCS_QasmOperand *bit) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_MEASURE, at);
  if (stmt) {
    stmt->measure.qubit = qubit;
    stmt->measure.bit = bit;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_barrier(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmOperand *operands) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_BARRIER, at);
  if (stmt) {
    stmt->barrier.operands = operands;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_if(QCS_Arena *arena, QCS_QasmToken at,
                               QCS_QasmOperand *bits, bool negate,
                               QCS_QasmExpr *value, QCS_QasmStmt *body,
                               QCS_QasmStmt *else_body) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_IF, at);
  if (stmt) {
    stmt->branch.bits = bits;
    stmt->branch.negate = negate;
    stmt->branch.value = value;
    stmt->branch.body = body;
    stmt->branch.else_body = else_body;
  }
  return stmt;
}

QCS_QasmStmt *qcs_qasm_stmt_reset(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmOperand *qubits) {
  QCS_QasmStmt *stmt = qcs_qasm_stmt_new(arena, QCS_QASM_STMT_RESET, at);
  if (stmt) {
    stmt->reset.qubits = qubits;
  }
  return stmt;
}

static const char *qcs_qasm_type_name(QCS_QasmType type) {
  switch (type) {
  case QCS_QASM_TYPE_INT:
    return "int";
  case QCS_QASM_TYPE_UINT:
    return "uint";
  case QCS_QASM_TYPE_FLOAT:
    return "float";
  case QCS_QASM_TYPE_ANGLE:
    return "angle";
  }
  return "?";
}

static void qcs_qasm_dump_token(QCS_QasmDump *dump, QCS_QasmToken token) {
  qcs_qasm_dump_append(dump, "%.*s", (int)token.length, token.start);
}

// start:end or start:step:end
static void qcs_qasm_dump_range(QCS_QasmDump *dump, QCS_QasmRange range) {
  qcs_qasm_dump_expr(dump, range.start);
  if (range.step) {
    qcs_qasm_dump_append(dump, ":");
    qcs_qasm_dump_expr(dump, range.step);
  }
  qcs_qasm_dump_append(dump, ":");
  qcs_qasm_dump_expr(dump, range.end);
}

// q, q[i], q[a:b] separated by spaces, in parentheses
static void qcs_qasm_dump_operands(QCS_QasmDump *dump,
                                   const QCS_QasmOperand *operand) {
  qcs_qasm_dump_append(dump, "(");
  for (; operand; operand = operand->next) {
    qcs_qasm_dump_token(dump, operand->name);
    if (operand->kind == QCS_QASM_INDEX_ONE) {
      qcs_qasm_dump_append(dump, "[");
      qcs_qasm_dump_expr(dump, operand->index);
      qcs_qasm_dump_append(dump, "]");
    } else if (operand->kind == QCS_QASM_INDEX_RANGE) {
      qcs_qasm_dump_append(dump, "[");
      qcs_qasm_dump_range(dump, operand->range);
      qcs_qasm_dump_append(dump, "]");
    }
    qcs_qasm_dump_append(dump, operand->next ? " " : "");
  }
  qcs_qasm_dump_append(dump, ")");
}

// expressions separated by spaces, in open and close
static void qcs_qasm_dump_exprs(QCS_QasmDump *dump, const QCS_QasmExpr *expr,
                                const char *open, const char *close) {
  qcs_qasm_dump_append(dump, "%s", open);
  for (; expr; expr = expr->next) {
    qcs_qasm_dump_expr(dump, expr);
    qcs_qasm_dump_append(dump, expr->next ? " " : "");
  }
  qcs_qasm_dump_append(dump, "%s", close);
}

static void qcs_qasm_dump_names(QCS_QasmDump *dump, const QCS_QasmName *name) {
  qcs_qasm_dump_append(dump, "(");
  for (; name; name = name->next) {
    qcs_qasm_dump_token(dump, name->token);
    qcs_qasm_dump_append(dump, name->next ? " " : "");
  }
  qcs_qasm_dump_append(dump, ")");
}

static const char *qcs_qasm_modifier_kind_name(QCS_QasmModifierKind kind) {
  switch (kind) {
  case QCS_QASM_MODIFIER_CTRL:
    return "ctrl";
  case QCS_QASM_MODIFIER_NEGCTRL:
    return "negctrl";
  case QCS_QASM_MODIFIER_INV:
    return "inv";
  case QCS_QASM_MODIFIER_POW:
    return "pow";
  }
  return "?";
}

static void qcs_qasm_dump_stmt(QCS_QasmDump *dump, const QCS_QasmStmt *stmt);

// the statements of a body, each after a space
static void qcs_qasm_dump_body(QCS_QasmDump *dump, const QCS_QasmStmt *stmt) {
  for (; stmt; stmt = stmt->next) {
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_stmt(dump, stmt);
  }
}

static void qcs_qasm_dump_stmt(QCS_QasmDump *dump, const QCS_QasmStmt *stmt) {
  switch (stmt->kind) {
  case QCS_QASM_STMT_INCLUDE:
    qcs_qasm_dump_append(dump, "(include \"");
    qcs_qasm_dump_token(dump, stmt->include);
    qcs_qasm_dump_append(dump, "\")");
    break;
  case QCS_QASM_STMT_DECLARE:
    qcs_qasm_dump_append(dump, stmt->declare.quantum ? "(qubit " : "(bit ");
    qcs_qasm_dump_token(dump, stmt->declare.name);
    if (stmt->declare.size) {
      qcs_qasm_dump_append(dump, " ");
      qcs_qasm_dump_expr(dump, stmt->declare.size);
    }
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_CONST:
    qcs_qasm_dump_append(dump, "(const %s ",
                         qcs_qasm_type_name(stmt->constant.type));
    qcs_qasm_dump_token(dump, stmt->constant.name);
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_expr(dump, stmt->constant.value);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_INPUT:
    qcs_qasm_dump_append(dump, "(input %s ",
                         qcs_qasm_type_name(stmt->input.type));
    qcs_qasm_dump_token(dump, stmt->input.name);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_GATE_CALL:
    qcs_qasm_dump_append(dump, "(call ");
    qcs_qasm_dump_token(dump, stmt->call.name);
    qcs_qasm_dump_exprs(dump, stmt->call.params, " (", ") ");
    qcs_qasm_dump_operands(dump, stmt->call.operands);
    for (const QCS_QasmModifier *m = stmt->call.modifiers; m; m = m->next) {
      qcs_qasm_dump_append(dump, " (%s", qcs_qasm_modifier_kind_name(m->kind));
      if (m->argument) {
        qcs_qasm_dump_append(dump, " ");
        qcs_qasm_dump_expr(dump, m->argument);
      }
      qcs_qasm_dump_append(dump, ")");
    }
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_GATE_DEF:
    qcs_qasm_dump_append(dump, "(gate ");
    qcs_qasm_dump_token(dump, stmt->gate.name);
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_names(dump, stmt->gate.params);
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_names(dump, stmt->gate.args);
    qcs_qasm_dump_body(dump, stmt->gate.body);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_FOR_RANGE:
  case QCS_QASM_STMT_FOR_SET:
    qcs_qasm_dump_append(dump, "(for %s ", qcs_qasm_type_name(stmt->loop.type));
    qcs_qasm_dump_token(dump, stmt->loop.variable);
    if (stmt->kind == QCS_QASM_STMT_FOR_RANGE) {
      qcs_qasm_dump_append(dump, " [");
      qcs_qasm_dump_range(dump, stmt->loop.range);
      qcs_qasm_dump_append(dump, "]");
    } else {
      qcs_qasm_dump_exprs(dump, stmt->loop.set, " {", "}");
    }
    qcs_qasm_dump_body(dump, stmt->loop.body);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_MEASURE:
    qcs_qasm_dump_append(dump, "(measure ");
    qcs_qasm_dump_operands(dump, stmt->measure.qubit);
    qcs_qasm_dump_append(dump, " ");
    qcs_qasm_dump_operands(dump, stmt->measure.bit);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_BARRIER:
    qcs_qasm_dump_append(dump, "(barrier ");
    qcs_qasm_dump_operands(dump, stmt->barrier.operands);
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_IF:
    // (if (c[0]) ...), (if (! (c[0])) ...), (if (== (c) 2) ...)
    qcs_qasm_dump_append(dump, "(if ");
    if (stmt->branch.negate) {
      qcs_qasm_dump_append(dump, "(! ");
      qcs_qasm_dump_operands(dump, stmt->branch.bits);
      qcs_qasm_dump_append(dump, ")");
    } else if (stmt->branch.value) {
      qcs_qasm_dump_append(dump, "(== ");
      qcs_qasm_dump_operands(dump, stmt->branch.bits);
      qcs_qasm_dump_append(dump, " ");
      qcs_qasm_dump_expr(dump, stmt->branch.value);
      qcs_qasm_dump_append(dump, ")");
    } else {
      qcs_qasm_dump_operands(dump, stmt->branch.bits);
    }
    qcs_qasm_dump_body(dump, stmt->branch.body);
    if (stmt->branch.else_body) {
      qcs_qasm_dump_append(dump, " (else");
      qcs_qasm_dump_body(dump, stmt->branch.else_body);
      qcs_qasm_dump_append(dump, ")");
    }
    qcs_qasm_dump_append(dump, ")");
    break;
  case QCS_QASM_STMT_RESET:
    qcs_qasm_dump_append(dump, "(reset ");
    qcs_qasm_dump_operands(dump, stmt->reset.qubits);
    qcs_qasm_dump_append(dump, ")");
    break;
  }
}

size_t qcs_qasm_stmt_dump(const QCS_QasmStmt *stmt, char *out, size_t size) {
  QCS_QasmDump dump = {.out = out, .size = size};
  if (size > 0) {
    out[0] = '\0';
  }
  qcs_qasm_dump_stmt(&dump, stmt);
  return dump.length;
}
