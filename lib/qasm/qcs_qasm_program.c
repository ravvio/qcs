#include "qcs_qasm_program.h"
#include "qcs_qasm_parser.h"

#include <math.h>
#include <qcs/qcs.h>
#include <stb_ds.h>
#include <stdarg.h>
#include <stdckdint.h>
#include <stdio.h>
#include <string.h>

// Most gates in a program, and in one call
#define QCS_QASM_MAX_GATES (1u << 18)
// Most parameters of a gate definition
#define QCS_QASM_MAX_PARAMS 32
// Most gate bodies run inside one another
#define QCS_QASM_MAX_CALL_DEPTH 32
// Most iterations of one loop
#define QCS_QASM_MAX_ITERATIONS (1u << 20)

// Gates of a call, a body or a statement, and the global phase they carry
typedef struct {
  QCS_Gate *list; // stb_ds
  double phase;   // e^{i·phase} on the whole state
} QCS_QasmGates;

// Gate an OpenQASM call can name: parameter and qubit counts, builder of
// its gates
typedef struct {
  const char *name;
  uint8_t param_count, qubit_count;
  // append the gates of one call on qubits q into out
  void (*build)(const uint32_t *q, const double *p, QCS_QasmGates *out);
} QCS_QasmGate;

// append gate into out, named name with param_count of the parameters p
static void qcs_qasm_gates_put(QCS_QasmGates *out, QCS_Gate gate,
                               const char *name, const double *p,
                               uint8_t param_count) {
  qcs_gate_set_label(&gate, name, p, param_count);
  arrput(out->list, gate);
}

static void qcs_qasm_gate_u(const uint32_t *q, const double *p,
                            QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_u(q[0], p[0], p[1], p[2]), "U", p, 3);
}
static void qcs_qasm_gate_id(const uint32_t *q, const double *,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_id(q[0]));
}
static void qcs_qasm_gate_x(const uint32_t *q, const double *,
                            QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_x(q[0]));
}
static void qcs_qasm_gate_y(const uint32_t *q, const double *,
                            QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_y(q[0]));
}
static void qcs_qasm_gate_z(const uint32_t *q, const double *,
                            QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_z(q[0]));
}
static void qcs_qasm_gate_h(const uint32_t *q, const double *,
                            QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_h(q[0]));
}
static void qcs_qasm_gate_sx(const uint32_t *q, const double *,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_sx(q[0]));
}
static void qcs_qasm_gate_s(const uint32_t *q, const double *p,
                            QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], QCS_PI / 2), "s", p, 0);
}
static void qcs_qasm_gate_sdg(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], -QCS_PI / 2), "sdg", p, 0);
}
static void qcs_qasm_gate_t(const uint32_t *q, const double *p,
                            QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], QCS_PI / 4), "t", p, 0);
}
static void qcs_qasm_gate_tdg(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], -QCS_PI / 4), "tdg", p, 0);
}
static void qcs_qasm_gate_p(const uint32_t *q, const double *p,
                            QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_p(q[0], p[0]));
}
static void qcs_qasm_gate_phase(const uint32_t *q, const double *p,
                                QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], p[0]), "phase", p, 1);
}
static void qcs_qasm_gate_u1(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], p[0]), "u1", p, 1);
}
static void qcs_qasm_gate_rx(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_rx(q[0], p[0]));
}
static void qcs_qasm_gate_ry(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_ry(q[0], p[0]));
}
static void qcs_qasm_gate_rz(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_rz(q[0], p[0]));
}
// make e^{-i(φ+λ+θ)/2}·U(θ, φ, λ), the OpenQASM 2 U
static void qcs_qasm_gate_u3(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  QCS_Gate u = qcs_gate_create_u(q[0], p[0], p[1], p[2]);
  qcs_qasm_gates_put(out, qcs_gate_with_phase(u, -(p[0] + p[1] + p[2]) / 2),
                     "u3", p, 3);
}
// make u3(π/2, φ, λ)
static void qcs_qasm_gate_u2(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  const double u3[3] = {QCS_PI / 2, p[0], p[1]};
  qcs_qasm_gate_u3(q, u3, out);
  qcs_gate_set_label(&arrlast(out->list), "u2", p, 2);
}
static void qcs_qasm_gate_cx(const uint32_t *q, const double *,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_cx(q[1], q[0]));
}
static void qcs_qasm_gate_CX(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_cx(q[1], q[0]), "CX", p, 0);
}
static void qcs_qasm_gate_cy(const uint32_t *q, const double *,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_cy(q[1], q[0]));
}
static void qcs_qasm_gate_cz(const uint32_t *q, const double *,
                             QCS_QasmGates *out) {
  arrput(out->list, qcs_gate_create_cz(q[1], q[0]));
}
// append gate controlled by c, named name with param_count of the
// parameters p
static void qcs_qasm_gates_put_controlled(QCS_QasmGates *out, QCS_Gate gate,
                                          uint32_t c, const char *name,
                                          const double *p,
                                          uint8_t param_count) {
  qcs_gate_add_control_one(&gate, c);
  qcs_qasm_gates_put(out, gate, name, p, param_count);
}
static void qcs_qasm_gate_ch(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_h(q[1]), q[0], "ch", p, 0);
}
static void qcs_qasm_gate_cp(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_p(q[1], p[0]), q[0], "cp",
                                p, 1);
}
static void qcs_qasm_gate_cphase(const uint32_t *q, const double *p,
                                 QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_p(q[1], p[0]), q[0],
                                "cphase", p, 1);
}
static void qcs_qasm_gate_crx(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_rx(q[1], p[0]), q[0],
                                "crx", p, 1);
}
static void qcs_qasm_gate_cry(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_ry(q[1], p[0]), q[0],
                                "cry", p, 1);
}
static void qcs_qasm_gate_crz(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_rz(q[1], p[0]), q[0],
                                "crz", p, 1);
}
// apply P(γ − θ/2) on the control, then U controlled by it
static void qcs_qasm_gate_cu(const uint32_t *q, const double *p,
                             QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_p(q[0], p[3] - p[0] / 2), "cu", p, 4);
  qcs_qasm_gates_put_controlled(out, qcs_gate_create_u(q[1], p[0], p[1], p[2]),
                                q[0], "cu", p, 4);
}
static void qcs_qasm_gate_ccx(const uint32_t *q, const double *p,
                              QCS_QasmGates *out) {
  QCS_Gate x = qcs_gate_create_x(q[2]);
  qcs_gate_add_control_one(&x, q[1]);
  qcs_qasm_gates_put_controlled(out, x, q[0], "ccx", p, 0);
}
static void qcs_qasm_gate_swap(const uint32_t *q, const double *p,
                               QCS_QasmGates *out) {
  qcs_qasm_gates_put(out, qcs_gate_create_cx(q[1], q[0]), "swap", p, 0);
  qcs_qasm_gates_put(out, qcs_gate_create_cx(q[0], q[1]), "swap", p, 0);
  qcs_qasm_gates_put(out, qcs_gate_create_cx(q[1], q[0]), "swap", p, 0);
}
// swap the last two, each gate controlled by the first
static void qcs_qasm_gate_cswap(const uint32_t *q, const double *p,
                                QCS_QasmGates *out) {
  qcs_qasm_gate_swap(q + 1, p, out);
  for (ptrdiff_t g = arrlen(out->list) - 3; g < arrlen(out->list); g++) {
    qcs_gate_add_control_one(&out->list[g], q[0]);
    qcs_gate_set_label(&out->list[g], "cswap", p, 0);
  }
}

// add e^{iγ} to the global phase
static void qcs_qasm_gate_gphase(const uint32_t *, const double *p,
                                 QCS_QasmGates *out) {
  out->phase += p[0];
}

static const QCS_QasmGate qcs_qasm_builtin_u = {"U", 3, 1, qcs_qasm_gate_u};
static const QCS_QasmGate qcs_qasm_builtin_gphase = {"gphase", 1, 0,
                                                     qcs_qasm_gate_gphase};

// stdgates.inc, as standard matrices
static const QCS_QasmGate qcs_qasm_stdgates[] = {
    {"id", 0, 1, qcs_qasm_gate_id},
    {"x", 0, 1, qcs_qasm_gate_x},
    {"y", 0, 1, qcs_qasm_gate_y},
    {"z", 0, 1, qcs_qasm_gate_z},
    {"h", 0, 1, qcs_qasm_gate_h},
    {"sx", 0, 1, qcs_qasm_gate_sx},
    {"s", 0, 1, qcs_qasm_gate_s},
    {"sdg", 0, 1, qcs_qasm_gate_sdg},
    {"t", 0, 1, qcs_qasm_gate_t},
    {"tdg", 0, 1, qcs_qasm_gate_tdg},
    {"p", 1, 1, qcs_qasm_gate_p},
    {"phase", 1, 1, qcs_qasm_gate_phase},
    {"u1", 1, 1, qcs_qasm_gate_u1},
    {"rx", 1, 1, qcs_qasm_gate_rx},
    {"ry", 1, 1, qcs_qasm_gate_ry},
    {"rz", 1, 1, qcs_qasm_gate_rz},
    {"u2", 2, 1, qcs_qasm_gate_u2},
    {"u3", 3, 1, qcs_qasm_gate_u3},
    {"cx", 0, 2, qcs_qasm_gate_cx},
    {"CX", 0, 2, qcs_qasm_gate_CX},
    {"cy", 0, 2, qcs_qasm_gate_cy},
    {"cz", 0, 2, qcs_qasm_gate_cz},
    {"ch", 0, 2, qcs_qasm_gate_ch},
    {"cp", 1, 2, qcs_qasm_gate_cp},
    {"cphase", 1, 2, qcs_qasm_gate_cphase},
    {"crx", 1, 2, qcs_qasm_gate_crx},
    {"cry", 1, 2, qcs_qasm_gate_cry},
    {"crz", 1, 2, qcs_qasm_gate_crz},
    {"cu", 4, 2, qcs_qasm_gate_cu},
    {"ccx", 0, 3, qcs_qasm_gate_ccx},
    {"swap", 0, 2, qcs_qasm_gate_swap},
    {"cswap", 0, 3, qcs_qasm_gate_cswap},
};

// Integer or float result of an expression
typedef struct {
  bool is_int;
  union {
    int64_t integer;
    double number;
  };
} QCS_QasmValue;

typedef enum : uint8_t {
  QCS_QASM_SYMBOL_QUBITS,
  QCS_QASM_SYMBOL_BITS,
  QCS_QASM_SYMBOL_CONST,
  QCS_QASM_SYMBOL_INPUT,
  QCS_QASM_SYMBOL_GATE,
  QCS_QASM_SYMBOL_DEFINITION,
} QCS_QasmSymbolKind;

// Declared name: a register, a constant, an input, a gate or a gate
// definition
typedef struct {
  QCS_QasmSymbolKind kind;
  QCS_QasmToken name;
  union {
    struct {
      uint32_t offset, size; // global range
      bool single;           // qubit q; or bit c;, not indexable
    } reg;
    QCS_QasmValue value;
    const QCS_QasmGate *gate;
    struct {
      const QCS_QasmStmt *stmt; // gate name(params) args { body }
      uint32_t visible;         // symbols before it, what its body can see
      uint8_t param_count, qubit_count;
    } definition;
  };
} QCS_QasmSymbol;

// Call into a gate body: its arguments, and what its definition can see
typedef struct {
  QCS_QasmSymbol *bindings; // stb_ds: parameters as constants, qubits
  uint32_t visible;         // symbols before the definition
} QCS_QasmFrame;

typedef struct {
  QCS_QasmSymbol *symbols;         // stb_ds array, newest last
  bool stdgates;                   // stdgates.inc included
  QCS_QasmFrame *frame;            // gate body being checked or run, or NULL
  uint32_t depth;                  // gate bodies being run, one inside another
  uint32_t call_line, call_column; // outermost call into a gate body
  uint64_t iterations;             // loop iterations run
  uint32_t gate_count;             // gates in the program's runs
  const QCS_QasmInput *inputs;     // values for the program's inputs
  size_t input_count;
  QCS_QasmProgram *out;
  QCS_QasmError error;
} QCS_QasmScope;

// tell if a gate body is walked at its definition: names and counts checked,
// no gates built
static bool qcs_qasm_scope_at_definition(const QCS_QasmScope *scope) {
  return scope->frame && scope->depth == 0;
}

// count the parameters of a gate or a definition
static uint32_t qcs_qasm_symbol_param_count(const QCS_QasmSymbol *gate) {
  return gate->kind == QCS_QASM_SYMBOL_GATE ? gate->gate->param_count
                                            : gate->definition.param_count;
}

// count the qubits of a gate or a definition
static uint32_t qcs_qasm_symbol_qubit_count(const QCS_QasmSymbol *gate) {
  return gate->kind == QCS_QASM_SYMBOL_GATE ? gate->gate->qubit_count
                                            : gate->definition.qubit_count;
}

// set the error at line:column, return false
static bool qcs_qasm_scope_set_error_at(QCS_QasmScope *scope, uint32_t line,
                                        uint32_t column, const char *format,
                                        ...) {
  scope->error.line = line;
  scope->error.column = column;
  va_list args;
  va_start(args, format);
  vsnprintf(scope->error.message, sizeof scope->error.message, format, args);
  va_end(args);

  // in a gate body, name the outermost call
  if (scope->depth > 0) {
    size_t length = strlen(scope->error.message);
    snprintf(scope->error.message + length,
             sizeof scope->error.message - length, " (in call at %u:%u)",
             scope->call_line, scope->call_column);
  }
  return false;
}

// compare the text of two tokens
static bool qcs_qasm_token_same_text(QCS_QasmToken a, QCS_QasmToken b) {
  return a.length == b.length && memcmp(a.start, b.start, a.length) == 0;
}

// make an identifier token for a C string
static QCS_QasmToken qcs_qasm_token_from_text(const char *text) {
  return (QCS_QasmToken){.kind = QCS_QASM_TOKEN_IDENT,
                         .start = text,
                         .length = (uint32_t)strlen(text)};
}

// find the binding named by token, or NULL
static QCS_QasmSymbol *qcs_qasm_frame_find(QCS_QasmFrame *frame,
                                           QCS_QasmToken name) {
  for (ptrdiff_t i = arrlen(frame->bindings) - 1; i >= 0; i--) {
    if (qcs_qasm_token_same_text(frame->bindings[i].name, name)) {
      return &frame->bindings[i];
    }
  }
  return nullptr;
}

// find the symbol named by token, newest first, or NULL
static QCS_QasmSymbol *qcs_qasm_scope_find(QCS_QasmScope *scope,
                                           QCS_QasmToken name) {
  // in a gate body: its arguments, then the constants and gates before it
  if (scope->frame) {
    QCS_QasmSymbol *binding = qcs_qasm_frame_find(scope->frame, name);
    if (binding) {
      return binding;
    }
    for (ptrdiff_t i = scope->frame->visible - 1; i >= 0; i--) {
      QCS_QasmSymbol *symbol = &scope->symbols[i];
      bool hidden = symbol->kind == QCS_QASM_SYMBOL_QUBITS ||
                    symbol->kind == QCS_QASM_SYMBOL_BITS ||
                    symbol->kind == QCS_QASM_SYMBOL_INPUT;
      if (!hidden && qcs_qasm_token_same_text(symbol->name, name)) {
        return symbol;
      }
    }
    return nullptr;
  }

  // at the top level: every symbol
  for (ptrdiff_t i = arrlen(scope->symbols) - 1; i >= 0; i--) {
    if (qcs_qasm_token_same_text(scope->symbols[i].name, name)) {
      return &scope->symbols[i];
    }
  }
  return nullptr;
}

// add a symbol, unless its name is already declared
static bool qcs_qasm_scope_declare(QCS_QasmScope *scope,
                                   QCS_QasmSymbol symbol) {
  if (qcs_qasm_scope_find(scope, symbol.name)) {
    return qcs_qasm_scope_set_error_at(
        scope, symbol.name.line, symbol.name.column, "%.*s is already declared",
        (int)symbol.name.length, symbol.name.start);
  }
  arrput(scope->symbols, symbol);
  return true;
}

// convert a value to a float
static double qcs_qasm_value_as_float(QCS_QasmValue value) {
  return value.is_int ? (double)value.integer : value.number;
}

// compute base ** exponent for exponent >= 0, return false on overflow
static bool qcs_int64_pow(int64_t base, int64_t exponent, int64_t *out) {
  int64_t result = 1;
  while (exponent > 0) {
    if ((exponent & 1) && ckd_mul(&result, result, base)) {
      return false;
    }
    exponent >>= 1;
    if (exponent > 0 && ckd_mul(&base, base, base)) {
      return false;
    }
  }
  *out = result;
  return true;
}

static bool qcs_qasm_scope_eval(QCS_QasmScope *scope, const QCS_QasmExpr *expr,
                                QCS_QasmValue *out);

// apply the integer op of expr to a and b, errors at expr
static bool qcs_qasm_scope_eval_int_binary(QCS_QasmScope *scope,
                                           const QCS_QasmExpr *expr, int64_t a,
                                           int64_t b, int64_t *out) {
  bool overflow = false;
  switch (expr->binary.op) {
  case QCS_QASM_OP_ADD:
    overflow = ckd_add(out, a, b);
    break;
  case QCS_QASM_OP_SUB:
    overflow = ckd_sub(out, a, b);
    break;
  case QCS_QASM_OP_MUL:
    overflow = ckd_mul(out, a, b);
    break;
  case QCS_QASM_OP_DIV:
  case QCS_QASM_OP_MOD:
    if (b == 0) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "division by zero");
    }
    overflow = a == INT64_MIN && b == -1;
    if (!overflow) {
      *out = expr->binary.op == QCS_QASM_OP_DIV ? a / b : a % b;
    }
    break;
  case QCS_QASM_OP_POW:
    overflow = !qcs_int64_pow(a, b, out);
    break;
  case QCS_QASM_OP_NEG:
    break;
  }
  if (overflow) {
    return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                       "integer overflow");
  }
  return true;
}

// apply a float op to a and b
static double qcs_qasm_op_apply_float(QCS_QasmOp op, double a, double b) {
  switch (op) {
  case QCS_QASM_OP_ADD:
    return a + b;
  case QCS_QASM_OP_SUB:
    return a - b;
  case QCS_QASM_OP_MUL:
    return a * b;
  case QCS_QASM_OP_DIV:
    return a / b;
  case QCS_QASM_OP_POW:
    return pow(a, b);
  case QCS_QASM_OP_MOD:
  case QCS_QASM_OP_NEG:
    break;
  }
  return NAN;
}

// find the constant or input a name expression names, or NULL with the error
// at it
static const QCS_QasmSymbol *
qcs_qasm_scope_find_const(QCS_QasmScope *scope, const QCS_QasmExpr *expr) {
  QCS_QasmSymbol *symbol = qcs_qasm_scope_find(scope, expr->name);
  if (!symbol) {
    qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                "unknown name %.*s", (int)expr->name.length,
                                expr->name.start);
    return nullptr;
  }
  if (symbol->kind != QCS_QASM_SYMBOL_CONST &&
      symbol->kind != QCS_QASM_SYMBOL_INPUT) {
    qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                "%.*s is not a value", (int)expr->name.length,
                                expr->name.start);
    return nullptr;
  }
  return symbol;
}

// check every name in expr is a constant
static bool qcs_qasm_scope_check_names(QCS_QasmScope *scope,
                                       const QCS_QasmExpr *expr) {
  switch (expr->kind) {
  case QCS_QASM_EXPR_INT:
  case QCS_QASM_EXPR_FLOAT:
    return true;
  case QCS_QASM_EXPR_NAME:
    return qcs_qasm_scope_find_const(scope, expr) != nullptr;
  case QCS_QASM_EXPR_UNARY:
    return qcs_qasm_scope_check_names(scope, expr->unary.operand);
  case QCS_QASM_EXPR_BINARY:
    return qcs_qasm_scope_check_names(scope, expr->binary.left) &&
           qcs_qasm_scope_check_names(scope, expr->binary.right);
  case QCS_QASM_EXPR_CALL:
    return qcs_qasm_scope_check_names(scope, expr->call.argument);
  }
  return false;
}

// evaluate expr, errors at the node that failed
static bool qcs_qasm_scope_eval(QCS_QasmScope *scope, const QCS_QasmExpr *expr,
                                QCS_QasmValue *out) {
  switch (expr->kind) {
  case QCS_QASM_EXPR_INT:
    *out = (QCS_QasmValue){.is_int = true, .integer = expr->integer};
    return true;
  case QCS_QASM_EXPR_FLOAT:
    *out = (QCS_QasmValue){.number = expr->number};
    return true;
  case QCS_QASM_EXPR_NAME: {
    const QCS_QasmSymbol *symbol = qcs_qasm_scope_find_const(scope, expr);
    if (!symbol) {
      return false;
    }
    *out = symbol->value;
    return true;
  }
  case QCS_QASM_EXPR_UNARY: {
    QCS_QasmValue operand;
    if (!qcs_qasm_scope_eval(scope, expr->unary.operand, &operand)) {
      return false;
    }
    if (!operand.is_int) {
      *out = (QCS_QasmValue){.number = -operand.number};
      return true;
    }
    *out = (QCS_QasmValue){.is_int = true};
    if (ckd_sub(&out->integer, (int64_t)0, operand.integer)) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "integer overflow");
    }
    return true;
  }
  case QCS_QASM_EXPR_BINARY: {
    QCS_QasmValue a, b;
    if (!qcs_qasm_scope_eval(scope, expr->binary.left, &a) ||
        !qcs_qasm_scope_eval(scope, expr->binary.right, &b)) {
      return false;
    }
    QCS_QasmOp op = expr->binary.op;
    // keep integers integers, except for a negative power
    if (a.is_int && b.is_int && !(op == QCS_QASM_OP_POW && b.integer < 0)) {
      *out = (QCS_QasmValue){.is_int = true};
      return qcs_qasm_scope_eval_int_binary(scope, expr, a.integer, b.integer,
                                            &out->integer);
    }
    if (op == QCS_QASM_OP_MOD) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "%% needs integers");
    }
    *out = (QCS_QasmValue){
        .number = qcs_qasm_op_apply_float(op, qcs_qasm_value_as_float(a),
                                          qcs_qasm_value_as_float(b))};
    break;
  }
  case QCS_QASM_EXPR_CALL: {
    QCS_QasmValue argument;
    if (!qcs_qasm_scope_eval(scope, expr->call.argument, &argument)) {
      return false;
    }
    const QCS_QasmFunction *function =
        qcs_qasm_token_function(&expr->call.name);
    *out = (QCS_QasmValue){
        .number = function->apply(qcs_qasm_value_as_float(argument))};
    break;
  }
  }
  // check float results
  if (!isfinite(out->number)) {
    return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                       "not a finite number");
  }
  return true;
}

// evaluate expr as an integer, errors at it
static bool qcs_qasm_scope_eval_integer(QCS_QasmScope *scope,
                                        const QCS_QasmExpr *expr,
                                        int64_t *out) {
  QCS_QasmValue value;
  if (!qcs_qasm_scope_eval(scope, expr, &value)) {
    return false;
  }
  if (!value.is_int) {
    return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                       "expected an integer");
  }
  *out = value.integer;
  return true;
}

// find the stdgates.inc gate named by token, or NULL
static const QCS_QasmGate *qcs_qasm_token_stdgate(QCS_QasmToken name) {
  for (size_t i = 0; i < sizeof qcs_qasm_stdgates / sizeof *qcs_qasm_stdgates;
       i++) {
    if (qcs_qasm_token_same_text(
            name, qcs_qasm_token_from_text(qcs_qasm_stdgates[i].name))) {
      return &qcs_qasm_stdgates[i];
    }
  }
  return nullptr;
}

// include "stdgates.inc": declare its gates
static bool qcs_qasm_scope_include(QCS_QasmScope *scope,
                                   const QCS_QasmStmt *stmt) {
  // check it's included once
  if (scope->stdgates) {
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "stdgates.inc is already included");
  }
  scope->stdgates = true;

  // declare each gate, at the include
  for (size_t i = 0; i < sizeof qcs_qasm_stdgates / sizeof *qcs_qasm_stdgates;
       i++) {
    QCS_QasmSymbol symbol = {
        .kind = QCS_QASM_SYMBOL_GATE,
        .name = qcs_qasm_token_from_text(qcs_qasm_stdgates[i].name),
        .gate = &qcs_qasm_stdgates[i]};
    symbol.name.line = stmt->line;
    symbol.name.column = stmt->column;
    if (!qcs_qasm_scope_declare(scope, symbol)) {
      return false;
    }
  }
  return true;
}

// declare a qubit or bit register at the next offset
static bool qcs_qasm_scope_declare_register(QCS_QasmScope *scope,
                                            const QCS_QasmStmt *stmt) {
  QCS_QasmProgram *out = scope->out;
  QCS_QasmToken name = stmt->declare.name;
  bool quantum = stmt->declare.quantum;

  // check the name fits the program's register
  if (name.length >= QCS_QASM_REGISTER_NAME_BUFFER_LENGTH) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column,
        "register name longer than %d characters",
        QCS_QASM_REGISTER_NAME_BUFFER_LENGTH - 1);
  }

  // get the size, 1 for qubit q; and bit c;
  int64_t size = 1;
  const QCS_QasmExpr *size_expr = stmt->declare.size;
  if (size_expr) {
    if (!qcs_qasm_scope_eval_integer(scope, size_expr, &size)) {
      return false;
    }
    if (size < 1) {
      return qcs_qasm_scope_set_error_at(scope, size_expr->line,
                                         size_expr->column,
                                         "register size must be at least 1");
    }
  }

  // check the total of qubits or bits
  uint32_t *count =
      quantum ? &out->program.qubit_count : &out->program.clbit_count;
  int64_t limit = quantum ? QCS_MAX_QUBITS : QCS_MAX_CLBITS;
  if (*count + size > limit) {
    uint32_t line = size_expr ? size_expr->line : name.line;
    uint32_t column = size_expr ? size_expr->column : name.column;
    return qcs_qasm_scope_set_error_at(scope, line, column, "more than %lld %s",
                                       (long long)limit,
                                       quantum ? "qubits" : "bits");
  }

  // declare the name
  QCS_QasmSymbol symbol = {.kind = quantum ? QCS_QASM_SYMBOL_QUBITS
                                           : QCS_QASM_SYMBOL_BITS,
                           .name = name,
                           .reg = {.offset = *count,
                                   .size = (uint32_t)size,
                                   .single = size_expr == nullptr}};
  if (!qcs_qasm_scope_declare(scope, symbol)) {
    return false;
  }

  // add the register to the program
  QCS_QasmRegister reg = {.offset = *count, .size = (uint32_t)size};
  memcpy(reg.name, name.start, name.length);
  if (quantum) {
    arrput(out->qregs, reg);
  } else {
    arrput(out->cregs, reg);
  }
  *count += (uint32_t)size;
  return true;
}

// declare a constant: const type name = value;
static bool qcs_qasm_scope_const(QCS_QasmScope *scope,
                                 const QCS_QasmStmt *stmt) {
  // evaluate the value
  const QCS_QasmExpr *expr = stmt->constant.value;
  QCS_QasmValue value;
  if (!qcs_qasm_scope_eval(scope, expr, &value)) {
    return false;
  }

  // check it against the type: integers for int and uint, floats otherwise
  QCS_QasmType type = stmt->constant.type;
  if (type == QCS_QASM_TYPE_INT || type == QCS_QASM_TYPE_UINT) {
    if (!value.is_int) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "expected an integer");
    }
    if (type == QCS_QASM_TYPE_UINT && value.integer < 0) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "uint can't be negative");
    }
  } else if (value.is_int) {
    value = (QCS_QasmValue){.number = (double)value.integer};
  }
  return qcs_qasm_scope_declare(scope,
                                (QCS_QasmSymbol){.kind = QCS_QASM_SYMBOL_CONST,
                                                 .name = stmt->constant.name,
                                                 .value = value});
}

// Global indices an operand names, qubits or bits: start, start + step, ...,
// count of them; a list for a register or a slice, else one index
typedef struct {
  int64_t start, step;
  uint32_t count;
  bool list;
} QCS_QasmIndices;

// evaluate an index into a register of size, check it's in 0 .. size - 1
static bool qcs_qasm_scope_eval_index(QCS_QasmScope *scope,
                                      const QCS_QasmExpr *expr,
                                      QCS_QasmToken name, uint32_t size,
                                      int64_t *out) {
  if (!qcs_qasm_scope_eval_integer(scope, expr, out)) {
    return false;
  }
  if (*out < 0 || *out >= size) {
    return qcs_qasm_scope_set_error_at(
        scope, expr->line, expr->column,
        "index %lld out of range, %.*s has size %u", (long long)*out,
        (int)name.length, name.start, size);
  }
  return true;
}

// resolve an operand of a qubit or bit register (kind) into its global
// indices
static bool qcs_qasm_scope_resolve(QCS_QasmScope *scope,
                                   const QCS_QasmOperand *operand,
                                   QCS_QasmSymbolKind kind,
                                   QCS_QasmIndices *out) {
  // find the register
  QCS_QasmToken name = operand->name;
  const char *what = kind == QCS_QASM_SYMBOL_QUBITS ? "qubit" : "bit";
  QCS_QasmSymbol *symbol = qcs_qasm_scope_find(scope, name);
  if (!symbol) {
    return qcs_qasm_scope_set_error_at(scope, name.line, name.column,
                                       "unknown register %.*s",
                                       (int)name.length, name.start);
  }
  if (symbol->kind != kind) {
    return qcs_qasm_scope_set_error_at(scope, name.line, name.column,
                                       "%.*s is not %ss", (int)name.length,
                                       name.start, what);
  }
  if (operand->kind != QCS_QASM_INDEX_NONE && symbol->reg.single) {
    return qcs_qasm_scope_set_error_at(scope, name.line, name.column,
                                       "%.*s is a single %s", (int)name.length,
                                       name.start, what);
  }
  uint32_t offset = symbol->reg.offset;
  uint32_t size = symbol->reg.size;

  // whole register
  if (operand->kind == QCS_QASM_INDEX_NONE) {
    *out = (QCS_QasmIndices){
        .start = offset, .step = 1, .count = size, .list = !symbol->reg.single};
    return true;
  }

  // one index
  if (operand->kind == QCS_QASM_INDEX_ONE) {
    int64_t index;
    if (!qcs_qasm_scope_eval_index(scope, operand->index, name, size, &index)) {
      return false;
    }
    *out = (QCS_QasmIndices){.start = offset + index, .step = 1, .count = 1};
    return true;
  }

  // slice: start, start + step, ... up to end, end included
  QCS_QasmRange range = operand->range;
  int64_t start, end, step = 1;
  if (!qcs_qasm_scope_eval_index(scope, range.start, name, size, &start) ||
      !qcs_qasm_scope_eval_index(scope, range.end, name, size, &end) ||
      (range.step && !qcs_qasm_scope_eval_integer(scope, range.step, &step))) {
    return false;
  }
  if (step == 0) {
    return qcs_qasm_scope_set_error_at(scope, range.step->line,
                                       range.step->column, "step can't be 0");
  }
  if ((step > 0 && end < start) || (step < 0 && end > start)) {
    return qcs_qasm_scope_set_error_at(scope, range.start->line,
                                       range.start->column, "empty range");
  }
  *out = (QCS_QasmIndices){.start = offset + start,
                           .step = step,
                           .count = (uint32_t)((end - start) / step + 1),
                           .list = true};
  return true;
}

// Modifier with its argument evaluated: control count for ctrl and negctrl,
// exponent for pow
typedef struct {
  QCS_QasmModifierKind kind;
  int64_t count;
  uint32_t line, column;
} QCS_QasmModifierValue;

// evaluate the modifiers of a call into values (stb_ds array), in order,
// and count the controls they add
static bool qcs_qasm_scope_eval_modifiers(QCS_QasmScope *scope,
                                          const QCS_QasmModifier *modifier,
                                          QCS_QasmModifierValue **values,
                                          uint32_t *controls) {
  *controls = 0;
  for (; modifier; modifier = modifier->next) {
    // evaluate the argument, 1 when there's none
    QCS_QasmModifierValue value = {.kind = modifier->kind,
                                   .count = 1,
                                   .line = modifier->line,
                                   .column = modifier->column};
    const QCS_QasmExpr *argument = modifier->argument;
    if (argument &&
        !qcs_qasm_scope_eval_integer(scope, argument, &value.count)) {
      return false;
    }

    // check and count the controls
    if (modifier->kind == QCS_QASM_MODIFIER_CTRL ||
        modifier->kind == QCS_QASM_MODIFIER_NEGCTRL) {
      if (value.count < 1) {
        return qcs_qasm_scope_set_error_at(scope, argument->line,
                                           argument->column,
                                           "control count must be at least 1");
      }
      if (value.count > QCS_MAX_QUBITS - (int64_t)*controls) {
        return qcs_qasm_scope_set_error_at(
            scope, modifier->line, modifier->column, "more than %d controls",
            QCS_MAX_QUBITS);
      }
      *controls += (uint32_t)value.count;
    }
    arrput(*values, value);
  }
  return true;
}

// add the controls to every gate, on 1 or on 0, then turn the global phase
// into a phase on the first control, controlled by the others
static void qcs_qasm_gates_control(QCS_QasmGates *gates,
                                   const uint32_t *controls, uint32_t count,
                                   bool one) {
  // the gates
  for (ptrdiff_t g = arrlen(gates->list) - 1; g >= 0; g--) {
    for (uint32_t c = 0; c < count; c++) {
      QCS_Gate *gate = &gates->list[g];
      uint32_t control = controls[c];

      // a gate needing the opposite value never acts
      uint32_t opposite = one ? gate->control_zeros : gate->control_ones;
      if (opposite >> control & 1) {
        arrdel(gates->list, g);
        break;
      }

      // a diagonal gate controlled by its target: 1 on the other value
      bool diagonal = gate->m[0][1] == 0 && gate->m[1][0] == 0;
      if (control == gate->target && diagonal) {
        gate->m[one ? 0 : 1][one ? 0 : 1] = 1;
        continue;
      }

      if (one) {
        qcs_gate_add_control_one(gate, control);
      } else {
        qcs_gate_add_control_zero(gate, control);
      }
    }
  }

  // the phase: e^{iγ} on the 1, or the 0, of the first control
  if (gates->phase == 0 || count == 0) {
    return;
  }
  double complex phase = cexp(I * gates->phase);
  QCS_Gate gate = {.m = {{one ? 1 : phase, 0}, {0, one ? phase : 1}},
                   .target = controls[0]};
  for (uint32_t c = 1; c < count; c++) {
    if (one) {
      qcs_gate_add_control_one(&gate, controls[c]);
    } else {
      qcs_gate_add_control_zero(&gate, controls[c]);
    }
  }
  qcs_qasm_gates_put(gates, gate, "gphase", &gates->phase, 1);
  gates->phase = 0;
}

// stdgates.inc gates and the name of their inverse
static const struct {
  const char *name, *inverse;
} qcs_qasm_inverses[] = {
    {"s", "sdg"},       {"sdg", "s"}, {"t", "tdg"},   {"tdg", "t"},
    {"id", "id"},       {"x", "x"},   {"y", "y"},     {"z", "z"},
    {"h", "h"},         {"cx", "cx"}, {"CX", "CX"},   {"cy", "cy"},
    {"cz", "cz"},       {"ch", "ch"}, {"ccx", "ccx"}, {"swap", "swap"},
    {"cswap", "cswap"},
};

// gates whose inverse has their angles negated
static const char *qcs_qasm_rotations[] = {
    "rx", "ry",     "rz",  "p",   "phase", "u1",
    "cp", "cphase", "crx", "cry", "crz",   "gphase",
};

// reverse the gates, replace each with its adjoint, labelled as the inverse
// where known; negate the phase
static void qcs_qasm_gates_invert(QCS_QasmGates *gates) {
  gates->phase = -gates->phase;
  QCS_Gate *list = gates->list;

  // reverse
  ptrdiff_t n = arrlen(list);
  for (ptrdiff_t i = 0; i < n / 2; i++) {
    QCS_Gate swap = list[i];
    list[i] = list[n - 1 - i];
    list[n - 1 - i] = swap;
  }
  for (ptrdiff_t g = 0; g < n; g++) {
    // take the adjoint, which has no label
    QCS_Gate gate = list[g];
    list[g] = qcs_gate_adjoint(gate);
    if (!gate.name) {
      continue;
    }

    // name the inverse
    for (size_t i = 0; i < sizeof qcs_qasm_inverses / sizeof *qcs_qasm_inverses;
         i++) {
      if (strcmp(gate.name, qcs_qasm_inverses[i].name) == 0) {
        qcs_gate_set_label(&list[g], qcs_qasm_inverses[i].inverse, gate.params,
                           0);
      }
    }

    // keep the name of rotations, with negated angles
    double negated[QCS_GATE_MAX_PARAMS];
    for (uint8_t p = 0; p < gate.param_count; p++) {
      negated[p] = -gate.params[p];
    }
    for (size_t i = 0;
         i < sizeof qcs_qasm_rotations / sizeof *qcs_qasm_rotations; i++) {
      if (strcmp(gate.name, qcs_qasm_rotations[i]) == 0) {
        qcs_gate_set_label(&list[g], gate.name, negated, gate.param_count);
      }
    }

    // invert U(θ, φ, λ) to U(−θ, −λ, −φ)
    if (strcmp(gate.name, "U") == 0 || strcmp(gate.name, "u3") == 0 ||
        strcmp(gate.name, "u") == 0) {
      double inverse[3] = {-gate.params[0], -gate.params[2], -gate.params[1]};
      qcs_gate_set_label(&list[g], gate.name, inverse, 3);
    }
  }
}

// repeat the gates times times, none for 0, and the phase with them
static void qcs_qasm_gates_repeat(QCS_QasmGates *gates, uint64_t times) {
  gates->phase *= (double)times;

  // an empty list stays empty
  if (arrlen(gates->list) == 0) {
    return;
  }

  QCS_Gate *repeated = nullptr;
  for (uint64_t t = 0; t < times; t++) {
    for (ptrdiff_t g = 0; g < arrlen(gates->list); g++) {
      arrput(repeated, gates->list[g]);
    }
  }
  arrfree(gates->list);
  gates->list = repeated;
}

// append gates to into, at most QCS_QASM_MAX_GATES, add their phase, free
// them; errors at line:column
static bool qcs_qasm_scope_append_gates(QCS_QasmScope *scope,
                                        QCS_QasmGates *into,
                                        QCS_QasmGates *gates, uint32_t line,
                                        uint32_t column) {
  bool ok = (size_t)arrlen(gates->list) <=
            QCS_QASM_MAX_GATES - (size_t)arrlen(into->list);
  if (!ok) {
    qcs_qasm_scope_set_error_at(scope, line, column, "more than %u gates",
                                QCS_QASM_MAX_GATES);
  }
  for (ptrdiff_t g = 0; ok && g < arrlen(gates->list); g++) {
    arrput(into->list, gates->list[g]);
  }
  into->phase += gates->phase;
  arrfree(gates->list);
  *gates = (QCS_QasmGates){};
  return ok;
}

// check the operands of a barrier, emit nothing
static bool qcs_qasm_scope_barrier(QCS_QasmScope *scope,
                                   const QCS_QasmStmt *stmt) {
  for (const QCS_QasmOperand *o = stmt->barrier.operands; o; o = o->next) {
    QCS_QasmIndices qubits;
    if (!qcs_qasm_scope_resolve(scope, o, QCS_QASM_SYMBOL_QUBITS, &qubits)) {
      return false;
    }
  }
  return true;
}

// bind the parameters of a definition as constants, its arguments as qubits q
static void qcs_qasm_frame_bind(QCS_QasmFrame *frame,
                                const QCS_QasmStmt *definition,
                                const double *params, const uint32_t *q) {
  uint32_t i = 0;
  for (const QCS_QasmName *p = definition->gate.params; p; p = p->next) {
    QCS_QasmSymbol symbol = {.kind = QCS_QASM_SYMBOL_CONST,
                             .name = p->token,
                             .value = {.number = params[i++]}};
    arrput(frame->bindings, symbol);
  }
  i = 0;
  for (const QCS_QasmName *a = definition->gate.args; a; a = a->next) {
    QCS_QasmSymbol symbol = {
        .kind = QCS_QASM_SYMBOL_QUBITS,
        .name = a->token,
        .reg = {.offset = q[i++], .size = 1, .single = true}};
    arrput(frame->bindings, symbol);
  }
}

static bool qcs_qasm_scope_append_call(QCS_QasmScope *scope,
                                       const QCS_QasmStmt *stmt,
                                       QCS_QasmGates *into);

// append the gates of a gate body's statements into (stb_ds): calls and
// barriers, in order
static bool qcs_qasm_scope_append_body_statements(QCS_QasmScope *scope,
                                                  const QCS_QasmStmt *first,
                                                  QCS_QasmGates *into) {
  for (const QCS_QasmStmt *stmt = first; stmt; stmt = stmt->next) {
    bool ok = stmt->kind == QCS_QASM_STMT_GATE_CALL
                  ? qcs_qasm_scope_append_call(scope, stmt, into)
                  : qcs_qasm_scope_barrier(scope, stmt);
    if (!ok) {
      return false;
    }
  }
  return true;
}

// run the body of a definition on qubits q into gates (stb_ds)
static bool qcs_qasm_scope_append_body(QCS_QasmScope *scope,
                                       const QCS_QasmSymbol *definition,
                                       const uint32_t *q, const double *params,
                                       QCS_QasmGates *gates) {
  // bind the arguments in a new frame
  const QCS_QasmStmt *stmt = definition->definition.stmt;
  QCS_QasmFrame frame = {.visible = definition->definition.visible};
  qcs_qasm_frame_bind(&frame, stmt, params, q);

  // run the body in it
  QCS_QasmFrame *outer = scope->frame;
  scope->frame = &frame;
  scope->depth++;
  bool ok =
      qcs_qasm_scope_append_body_statements(scope, stmt->gate.body, gates);
  scope->depth--;
  scope->frame = outer;
  arrfree(frame.bindings);
  return ok;
}

// build the gates of one call on qubits q into gates (stb_ds): the gate's,
// then each modifier from the innermost out
static bool qcs_qasm_scope_expand(QCS_QasmScope *scope,
                                  const QCS_QasmSymbol *gate,
                                  const QCS_QasmModifierValue *modifiers,
                                  const uint32_t *q, uint32_t controls,
                                  const double *params, QCS_QasmGates *gates) {
  // build the gate on its qubits, after the controls: a row, or a body
  if (gate->kind == QCS_QASM_SYMBOL_GATE) {
    gate->gate->build(q + controls, params, gates);
  } else if (!qcs_qasm_scope_append_body(scope, gate, q + controls, params,
                                         gates)) {
    return false;
  }

  // apply the modifiers, innermost first: its controls are the operands just
  // before the gate's qubits
  uint32_t end = controls;
  for (ptrdiff_t m = arrlen(modifiers) - 1; m >= 0; m--) {
    QCS_QasmModifierValue modifier = modifiers[m];
    switch (modifier.kind) {
    case QCS_QASM_MODIFIER_CTRL:
    case QCS_QASM_MODIFIER_NEGCTRL:
      end -= (uint32_t)modifier.count;
      qcs_qasm_gates_control(gates, q + end, (uint32_t)modifier.count,
                             modifier.kind == QCS_QASM_MODIFIER_CTRL);
      break;
    case QCS_QASM_MODIFIER_INV:
      qcs_qasm_gates_invert(gates);
      break;
    case QCS_QASM_MODIFIER_POW: {
      uint64_t times = modifier.count < 0 ? 0 - (uint64_t)modifier.count
                                          : (uint64_t)modifier.count;
      uint64_t length = (uint64_t)arrlen(gates->list);
      if (length > 0 && times > QCS_QASM_MAX_GATES / length) {
        return qcs_qasm_scope_set_error_at(
            scope, modifier.line, modifier.column,
            "pow makes more than %u gates", QCS_QASM_MAX_GATES);
      }
      if (modifier.count < 0) {
        qcs_qasm_gates_invert(gates);
      }
      qcs_qasm_gates_repeat(gates, times);
      break;
    }
    }
  }
  return true;
}

// resolve the operands of a gate call, broadcast them, append the gates of
// each broadcast index into (stb_ds)
static bool
qcs_qasm_scope_call_operands(QCS_QasmScope *scope, const QCS_QasmStmt *stmt,
                             const QCS_QasmSymbol *gate, const double *params,
                             const QCS_QasmModifierValue *modifiers,
                             uint32_t controls, QCS_QasmGates *into) {
  QCS_QasmToken name = stmt->call.name;

  // check the qubit count: controls, then the gate's
  uint32_t expected = controls + qcs_qasm_symbol_qubit_count(gate);
  if (expected > QCS_MAX_QUBITS) {
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "more than %d qubits in one call",
                                       QCS_MAX_QUBITS);
  }

  // resolve the operands
  QCS_QasmIndices qubits[QCS_MAX_QUBITS];
  const QCS_QasmOperand *operands[QCS_MAX_QUBITS];
  uint32_t count = 0;
  for (const QCS_QasmOperand *o = stmt->call.operands; o; o = o->next) {
    if (count < expected) {
      if (!qcs_qasm_scope_resolve(scope, o, QCS_QASM_SYMBOL_QUBITS,
                                  &qubits[count])) {
        return false;
      }
      operands[count] = o;
    }
    count++;
  }
  if (count != expected && controls > 0) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column,
        "%.*s with %u control%s takes %u qubits, got %u", (int)name.length,
        name.start, controls, controls == 1 ? "" : "s", expected, count);
  }
  if (count != expected) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column, "%.*s takes %u qubits, got %u",
        (int)name.length, name.start, expected, count);
  }

  // find the broadcast length: the lists all have the same count, n
  uint32_t n = 1;
  const QCS_QasmOperand *first_list = nullptr;
  for (uint32_t j = 0; j < count; j++) {
    if (!qubits[j].list) {
      continue;
    }
    if (!first_list) {
      first_list = operands[j];
      n = qubits[j].count;
    } else if (qubits[j].count != n) {
      QCS_QasmToken at = operands[j]->name;
      QCS_QasmToken first = first_list->name;
      return qcs_qasm_scope_set_error_at(
          scope, at.line, at.column, "%.*s has %u qubits, %.*s has %u",
          (int)at.length, at.start, qubits[j].count, (int)first.length,
          first.start, n);
    }
  }

  // expand one call per broadcast index
  for (uint32_t k = 0; k < n; k++) {
    // take the k-th qubit of each list, check they're all different
    uint32_t q[QCS_MAX_QUBITS];
    for (uint32_t j = 0; j < count; j++) {
      int64_t index = qubits[j].list ? k : 0;
      q[j] = (uint32_t)(qubits[j].start + index * qubits[j].step);
      for (uint32_t i = 0; i < j; i++) {
        if (q[i] == q[j]) {
          QCS_QasmToken at = operands[j]->name;
          if (qcs_qasm_scope_at_definition(scope)) {
            return qcs_qasm_scope_set_error_at(scope, at.line, at.column,
                                               "%.*s is used twice",
                                               (int)at.length, at.start);
          }
          return qcs_qasm_scope_set_error_at(scope, at.line, at.column,
                                             "qubit %u is used twice", q[j]);
        }
      }
    }

    if (qcs_qasm_scope_at_definition(scope)) {
      continue;
    }

    // build its gates, append them
    QCS_QasmGates gates = {};
    bool ok = qcs_qasm_scope_expand(scope, gate, modifiers, q, controls, params,
                                    &gates) &&
              qcs_qasm_scope_append_gates(scope, into, &gates, name.line,
                                          name.column);
    arrfree(gates.list);
    if (!ok) {
      return false;
    }
  }
  return true;
}

// append gate call into (stb_ds)
static bool qcs_qasm_scope_append_call(QCS_QasmScope *scope,
                                       const QCS_QasmStmt *stmt,
                                       QCS_QasmGates *into) {
  QCS_QasmToken name = stmt->call.name;

  // outside gate bodies, name this call in errors from the bodies it runs
  if (scope->depth == 0) {
    scope->call_line = name.line;
    scope->call_column = name.column;
  }

  // find the gate symbol: a row or a definition
  QCS_QasmSymbol *gate = qcs_qasm_scope_find(scope, name);
  if (!gate || (gate->kind != QCS_QASM_SYMBOL_GATE &&
                gate->kind != QCS_QASM_SYMBOL_DEFINITION)) {
    bool hint = !scope->stdgates && qcs_qasm_token_stdgate(name);
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column, "unknown gate %.*s%s", (int)name.length,
        name.start, hint ? " (include \"stdgates.inc\" first)" : "");
  }

  // check the nesting of gate bodies
  if (gate->kind == QCS_QASM_SYMBOL_DEFINITION &&
      scope->depth == QCS_QASM_MAX_CALL_DEPTH) {
    return qcs_qasm_scope_set_error_at(scope, name.line, name.column,
                                       "gate calls nest more than %u deep",
                                       QCS_QASM_MAX_CALL_DEPTH);
  }

  // get parameters: their names at the definition, else their values
  double params[QCS_QASM_MAX_PARAMS];
  uint32_t expected = qcs_qasm_symbol_param_count(gate);
  uint32_t param_count = 0;
  for (const QCS_QasmExpr *p = stmt->call.params; p; p = p->next) {
    if (param_count < expected) {
      QCS_QasmValue value = {};
      bool ok = qcs_qasm_scope_at_definition(scope)
                    ? qcs_qasm_scope_check_names(scope, p)
                    : qcs_qasm_scope_eval(scope, p, &value);
      if (!ok) {
        return false;
      }
      params[param_count] = qcs_qasm_value_as_float(value);
    }
    param_count++;
  }
  if (param_count != expected) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column, "%.*s takes %u parameters, got %u",
        (int)name.length, name.start, expected, param_count);
  }

  // get modifiers, and the controls they put before the gate's qubits
  QCS_QasmModifierValue *modifiers = nullptr;
  uint32_t controls;
  bool ok = qcs_qasm_scope_eval_modifiers(scope, stmt->call.modifiers,
                                          &modifiers, &controls) &&
            qcs_qasm_scope_call_operands(scope, stmt, gate, params, modifiers,
                                         controls, into);
  arrfree(modifiers);
  return ok;
}

// push gates and their phase into the op list into (stb_ds) as a run, errors
// at line:column
static bool qcs_qasm_scope_push(QCS_QasmScope *scope,
                                const QCS_QasmGates *gates, QCS_Op **into,
                                uint32_t line, uint32_t column) {
  size_t count = (size_t)arrlen(gates->list);
  if (count == 0 && gates->phase == 0) {
    return true;
  }

  // check the program's total
  if (count > QCS_QASM_MAX_GATES - scope->gate_count) {
    return qcs_qasm_scope_set_error_at(scope, line, column,
                                       "more than %u gates in the program",
                                       QCS_QASM_MAX_GATES);
  }

  // check them in a circuit on the program's qubits, add the phase
  QCS_Circuit run;
  qcs_circuit_create(scope->out->program.qubit_count, &run);
  run.global_phase = gates->phase;
  for (size_t g = 0; g < count; g++) {
    if (qcs_circuit_push_gate(&run, gates->list[g]) != QCS_ERR_OK) {
      qcs_circuit_destroy(&run);
      return qcs_qasm_scope_set_error_at(scope, line, column, "invalid gate");
    }
  }

  // append it
  qcs_ops_append_circuit(into, &run);
  qcs_circuit_destroy(&run);
  scope->gate_count += (uint32_t)count;
  return true;
}

// define a gate: gate name(params) args { body }
static bool qcs_qasm_scope_gate(QCS_QasmScope *scope,
                                const QCS_QasmStmt *stmt) {
  QCS_QasmToken name = stmt->gate.name;

  // check the name is new
  if (qcs_qasm_scope_find(scope, name)) {
    return qcs_qasm_scope_set_error_at(scope, name.line, name.column,
                                       "%.*s is already declared",
                                       (int)name.length, name.start);
  }

  // count parameters and arguments
  uint32_t param_count = 0, qubit_count = 0;
  for (const QCS_QasmName *p = stmt->gate.params; p; p = p->next) {
    param_count++;
  }
  for (const QCS_QasmName *a = stmt->gate.args; a; a = a->next) {
    qubit_count++;
  }
  if (param_count > QCS_QASM_MAX_PARAMS) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column, "%.*s has more than %u parameters",
        (int)name.length, name.start, QCS_QASM_MAX_PARAMS);
  }
  if (qubit_count > QCS_MAX_QUBITS) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column, "%.*s has more than %d qubits",
        (int)name.length, name.start, QCS_MAX_QUBITS);
  }

  // bind them in a frame that sees the symbols before: parameters 0,
  // arguments qubits 0, 1, ...
  double params[QCS_QASM_MAX_PARAMS] = {};
  uint32_t q[QCS_MAX_QUBITS];
  for (uint32_t i = 0; i < qubit_count; i++) {
    q[i] = i;
  }
  QCS_QasmFrame frame = {.visible = (uint32_t)arrlen(scope->symbols)};
  qcs_qasm_frame_bind(&frame, stmt, params, q);

  // check the names are all different
  bool ok = true;
  for (ptrdiff_t i = 0; ok && i < arrlen(frame.bindings); i++) {
    for (ptrdiff_t j = 0; ok && j < i; j++) {
      QCS_QasmToken at = frame.bindings[i].name;
      if (qcs_qasm_token_same_text(frame.bindings[j].name, at)) {
        ok = qcs_qasm_scope_set_error_at(scope, at.line, at.column,
                                         "%.*s is already declared",
                                         (int)at.length, at.start);
      }
    }
  }

  // check the body
  if (ok) {
    scope->frame = &frame;
    QCS_QasmGates gates = {};
    ok = qcs_qasm_scope_append_body_statements(scope, stmt->gate.body, &gates);
    arrfree(gates.list);
    scope->frame = nullptr;
  }
  arrfree(frame.bindings);
  if (!ok) {
    return false;
  }

  // declare it
  QCS_QasmSymbol symbol = {.kind = QCS_QASM_SYMBOL_DEFINITION,
                           .name = name,
                           .definition = {.stmt = stmt,
                                          .visible = frame.visible,
                                          .param_count = (uint8_t)param_count,
                                          .qubit_count = (uint8_t)qubit_count}};
  arrput(scope->symbols, symbol);
  return true;
}

static bool qcs_qasm_scope_append_statements(QCS_QasmScope *scope,
                                             const QCS_QasmStmt *first,
                                             QCS_Op **into);

// append a for loop into (stb_ds): the body once per value of the variable
static bool qcs_qasm_scope_append_for(QCS_QasmScope *scope,
                                      const QCS_QasmStmt *stmt, QCS_Op **into) {
  // get the values: start, step and count of a range, or the set's
  int64_t start = 0, step = 1;
  uint64_t count = 0;
  int64_t *values = nullptr;
  if (stmt->kind == QCS_QASM_STMT_FOR_RANGE) {
    const QCS_QasmRange *range = &stmt->loop.range;
    int64_t end;
    if (!qcs_qasm_scope_eval_integer(scope, range->start, &start) ||
        !qcs_qasm_scope_eval_integer(scope, range->end, &end) ||
        (range->step &&
         !qcs_qasm_scope_eval_integer(scope, range->step, &step))) {
      return false;
    }
    if (step == 0) {
      return qcs_qasm_scope_set_error_at(scope, range->step->line,
                                         range->step->column, "step is 0");
    }
    bool empty = (step > 0 && end < start) || (step < 0 && end > start);
    if (!empty) {
      uint64_t distance = step > 0 ? (uint64_t)end - (uint64_t)start
                                   : (uint64_t)start - (uint64_t)end;
      uint64_t stride = step > 0 ? (uint64_t)step : 0 - (uint64_t)step;
      uint64_t steps = distance / stride;
      count = steps == UINT64_MAX ? UINT64_MAX : steps + 1;
    }
  } else {
    for (const QCS_QasmExpr *e = stmt->loop.set; e; e = e->next) {
      int64_t value;
      if (!qcs_qasm_scope_eval_integer(scope, e, &value)) {
        arrfree(values);
        return false;
      }
      arrput(values, value);
    }
    count = (uint64_t)arrlen(values);
  }

  // check the iterations of the whole program
  if (count > QCS_QASM_MAX_ITERATIONS - scope->iterations) {
    arrfree(values);
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "more than %u loop iterations",
                                       QCS_QASM_MAX_ITERATIONS);
  }
  scope->iterations += count;

  // push the variable, over any name it shadows
  QCS_QasmSymbol variable = {.kind = QCS_QASM_SYMBOL_CONST,
                             .name = stmt->loop.variable,
                             .value = {.is_int = true}};
  arrput(scope->symbols, variable);
  ptrdiff_t index = arrlen(scope->symbols) - 1;

  // run the body once per value
  bool ok = true;
  int64_t value = start;
  for (uint64_t k = 0; ok && k < count; k++) {
    scope->symbols[index].value.integer = values ? values[k] : value;
    ok = qcs_qasm_scope_append_statements(scope, stmt->loop.body, into);
    ckd_add(&value, value, step);
  }

  // remove the variable
  arrsetlen(scope->symbols, index);
  arrfree(values);
  return ok;
}

// declare an input: input type name; with its value from the caller
static bool qcs_qasm_scope_input(QCS_QasmScope *scope,
                                 const QCS_QasmStmt *stmt) {
  QCS_QasmToken name = stmt->input.name;

  // check the type: float or angle
  QCS_QasmType type = stmt->input.type;
  if (type != QCS_QASM_TYPE_FLOAT && type != QCS_QASM_TYPE_ANGLE) {
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "only float and angle inputs are "
                                       "supported");
  }

  // find its value
  const QCS_QasmInput *input = nullptr;
  for (size_t i = 0; !input && i < scope->input_count; i++) {
    QCS_QasmToken given = qcs_qasm_token_from_text(scope->inputs[i].name);
    if (qcs_qasm_token_same_text(given, name)) {
      input = &scope->inputs[i];
    }
  }
  if (!input) {
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "no value for input %.*s",
                                       (int)name.length, name.start);
  }
  if (!isfinite(input->value)) {
    return qcs_qasm_scope_set_error_at(scope, stmt->line, stmt->column,
                                       "input %.*s is not a finite number",
                                       (int)name.length, name.start);
  }

  // declare it
  return qcs_qasm_scope_declare(
      scope, (QCS_QasmSymbol){.kind = QCS_QASM_SYMBOL_INPUT,
                              .name = name,
                              .value = {.number = input->value}});
}

// append the measures of qubits into bits into (stb_ds), the k-th qubit into
// the k-th bit
static bool qcs_qasm_scope_append_measure(QCS_QasmScope *scope,
                                          const QCS_QasmStmt *stmt,
                                          QCS_Op **into) {
  // resolve the qubits and the bits
  QCS_QasmIndices qubits, bits;
  if (!qcs_qasm_scope_resolve(scope, stmt->measure.qubit,
                              QCS_QASM_SYMBOL_QUBITS, &qubits) ||
      !qcs_qasm_scope_resolve(scope, stmt->measure.bit, QCS_QASM_SYMBOL_BITS,
                              &bits)) {
    return false;
  }

  // check the counts match
  if (qubits.count != bits.count) {
    QCS_QasmToken at = stmt->measure.bit->name;
    QCS_QasmToken from = stmt->measure.qubit->name;
    return qcs_qasm_scope_set_error_at(
        scope, at.line, at.column, "%.*s has %u bits, %.*s has %u qubits",
        (int)at.length, at.start, bits.count, (int)from.length, from.start,
        qubits.count);
  }

  // append each pair
  for (uint32_t k = 0; k < qubits.count; k++) {
    qcs_ops_append_measure(into,
                           (uint32_t)(qubits.start + (int64_t)k * qubits.step),
                           (uint32_t)(bits.start + (int64_t)k * bits.step));
  }
  return true;
}

// append the resets of qubits into (stb_ds)
static bool qcs_qasm_scope_append_reset(QCS_QasmScope *scope,
                                        const QCS_QasmStmt *stmt,
                                        QCS_Op **into) {
  QCS_QasmIndices qubits;
  if (!qcs_qasm_scope_resolve(scope, stmt->reset.qubits, QCS_QASM_SYMBOL_QUBITS,
                              &qubits)) {
    return false;
  }
  for (uint32_t k = 0; k < qubits.count; k++) {
    qcs_ops_append_reset(into,
                         (uint32_t)(qubits.start + (int64_t)k * qubits.step));
  }
  return true;
}

// evaluate the condition of an if into the clbits it reads (mask) and the
// value they need
static bool qcs_qasm_scope_eval_condition(QCS_QasmScope *scope,
                                          const QCS_QasmStmt *stmt,
                                          uint64_t *mask, uint64_t *value) {
  // resolve the bits
  QCS_QasmIndices bits;
  QCS_QasmToken name = stmt->branch.bits->name;
  if (!qcs_qasm_scope_resolve(scope, stmt->branch.bits, QCS_QASM_SYMBOL_BITS,
                              &bits)) {
    return false;
  }

  // get the value they need: 1, 0 with !, or the value after ==
  uint64_t needed = stmt->branch.negate ? 0 : 1;
  const QCS_QasmExpr *expr = stmt->branch.value;
  if (expr) {
    int64_t integer;
    if (!qcs_qasm_scope_eval_integer(scope, expr, &integer)) {
      return false;
    }
    if (integer < 0 ||
        (bits.count < 64 && (uint64_t)integer >> bits.count != 0)) {
      return qcs_qasm_scope_set_error_at(scope, expr->line, expr->column,
                                         "%lld doesn't fit in %u bits",
                                         (long long)integer, bits.count);
    }
    needed = (uint64_t)integer;
  } else if (bits.count != 1) {
    return qcs_qasm_scope_set_error_at(
        scope, name.line, name.column,
        "%.*s has %u bits, compare it with ==", (int)name.length, name.start,
        bits.count);
  }

  // place bit k of the value at its clbit
  *mask = 0;
  *value = 0;
  for (uint32_t k = 0; k < bits.count; k++) {
    uint64_t clbit = 1ull << (bits.start + (int64_t)k * bits.step);
    *mask |= clbit;
    if (needed >> k & 1) {
      *value |= clbit;
    }
  }
  return true;
}

// append an if into (stb_ds): its body and else body as the branches of an
// IF op
static bool qcs_qasm_scope_append_if(QCS_QasmScope *scope,
                                     const QCS_QasmStmt *stmt, QCS_Op **into) {
  // evaluate the condition
  uint64_t mask, value;
  if (!qcs_qasm_scope_eval_condition(scope, stmt, &mask, &value)) {
    return false;
  }

  // build the branches
  QCS_Op *then_ops = nullptr, *else_ops = nullptr;
  if (!qcs_qasm_scope_append_statements(scope, stmt->branch.body, &then_ops) ||
      !qcs_qasm_scope_append_statements(scope, stmt->branch.else_body,
                                        &else_ops)) {
    qcs_ops_destroy(&then_ops);
    qcs_ops_destroy(&else_ops);
    return false;
  }

  // append the IF, moving them in
  qcs_ops_append_if(into, mask, value, &then_ops, &else_ops);
  return true;
}

// append the ops of one statement into (stb_ds)
static bool qcs_qasm_scope_append_statement(QCS_QasmScope *scope,
                                            const QCS_QasmStmt *stmt,
                                            QCS_Op **into) {
  switch (stmt->kind) {
  case QCS_QASM_STMT_INCLUDE:
    return qcs_qasm_scope_include(scope, stmt);
  case QCS_QASM_STMT_DECLARE:
    return qcs_qasm_scope_declare_register(scope, stmt);
  case QCS_QASM_STMT_CONST:
    return qcs_qasm_scope_const(scope, stmt);
  case QCS_QASM_STMT_GATE_CALL: {
    QCS_QasmGates gates = {};
    bool ok =
        qcs_qasm_scope_append_call(scope, stmt, &gates) &&
        qcs_qasm_scope_push(scope, &gates, into, stmt->line, stmt->column);
    arrfree(gates.list);
    return ok;
  }
  case QCS_QASM_STMT_BARRIER:
    return qcs_qasm_scope_barrier(scope, stmt);
  case QCS_QASM_STMT_INPUT:
    return qcs_qasm_scope_input(scope, stmt);
  case QCS_QASM_STMT_GATE_DEF:
    return qcs_qasm_scope_gate(scope, stmt);
  case QCS_QASM_STMT_FOR_RANGE:
  case QCS_QASM_STMT_FOR_SET:
    return qcs_qasm_scope_append_for(scope, stmt, into);
  case QCS_QASM_STMT_MEASURE:
    return qcs_qasm_scope_append_measure(scope, stmt, into);
  case QCS_QASM_STMT_RESET:
    return qcs_qasm_scope_append_reset(scope, stmt, into);
  case QCS_QASM_STMT_IF:
    return qcs_qasm_scope_append_if(scope, stmt, into);
  }
  return false;
}

// append the ops of statements into (stb_ds), in order
static bool qcs_qasm_scope_append_statements(QCS_QasmScope *scope,
                                             const QCS_QasmStmt *first,
                                             QCS_Op **into) {
  for (const QCS_QasmStmt *stmt = first; stmt; stmt = stmt->next) {
    if (!qcs_qasm_scope_append_statement(scope, stmt, into)) {
      return false;
    }
  }
  return true;
}

// declare the built-in names: the constants, U and gphase
static void qcs_qasm_scope_declare_builtins(QCS_QasmScope *scope) {
  // constants
  static const struct {
    const char *name;
    double value;
  } constants[] = {
      {"pi", QCS_PI},
      {"\xcf\x80", QCS_PI},
      {"tau", 2 * QCS_PI},
      {"\xcf\x84", 2 * QCS_PI},
      {"euler", 2.718281828459045235},
      {"\xe2\x84\x87", 2.718281828459045235},
  };
  for (size_t i = 0; i < sizeof constants / sizeof *constants; i++) {
    QCS_QasmSymbol symbol = {.kind = QCS_QASM_SYMBOL_CONST,
                             .name =
                                 qcs_qasm_token_from_text(constants[i].name),
                             .value = {.number = constants[i].value}};
    arrput(scope->symbols, symbol);
  }

  // gates
  QCS_QasmSymbol u = {.kind = QCS_QASM_SYMBOL_GATE,
                      .name = qcs_qasm_token_from_text("U"),
                      .gate = &qcs_qasm_builtin_u};
  arrput(scope->symbols, u);
  QCS_QasmSymbol gphase = {.kind = QCS_QASM_SYMBOL_GATE,
                           .name = qcs_qasm_token_from_text("gphase"),
                           .gate = &qcs_qasm_builtin_gphase};
  arrput(scope->symbols, gphase);
}

bool qcs_qasm_program_create(const QCS_QasmStmt *statements,
                             const QCS_QasmInput *inputs, size_t input_count,
                             QCS_QasmProgram *out, QCS_QasmError *error) {
  // start an empty program with the built-in names
  *out = (QCS_QasmProgram){0};
  QCS_QasmScope scope = {
      .inputs = inputs, .input_count = input_count, .out = out};
  qcs_qasm_scope_declare_builtins(&scope);

  // append the ops of the statements in order, stop at the first error
  bool ok =
      qcs_qasm_scope_append_statements(&scope, statements, &out->program.ops);

  // check every input given is declared
  for (size_t i = 0; ok && i < input_count; i++) {
    QCS_QasmToken name = qcs_qasm_token_from_text(inputs[i].name);
    QCS_QasmSymbol *symbol = qcs_qasm_scope_find(&scope, name);
    if (!symbol || symbol->kind != QCS_QASM_SYMBOL_INPUT) {
      ok = qcs_qasm_scope_set_error_at(&scope, 0, 0, "unknown input %s",
                                       inputs[i].name);
    }
  }

  // on error, leave the program empty
  arrfree(scope.symbols);
  if (!ok) {
    qcs_qasm_program_destroy(out);
    *error = scope.error;
  }
  return ok;
}

QCS_Error qcs_qasm_parse(const char *source, const QCS_QasmInput *inputs,
                         size_t input_count, QCS_QasmProgram *out,
                         QCS_QasmError *error) {
  // parse into a tree, build the program, free the tree
  QCS_Arena arena = {0};
  QCS_QasmStmt *statements;
  bool ok = qcs_qasm_parse_program(source, &arena, &statements, error);
  if (ok) {
    ok = qcs_qasm_program_create(statements, inputs, input_count, out, error);
  } else {
    *out = (QCS_QasmProgram){0};
  }
  qcs_arena_free(&arena);
  return ok ? QCS_ERR_OK : QCS_ERR_QASM_PARSE_FAILURE;
}

void qcs_qasm_program_fdump(const QCS_QasmProgram *program, FILE *file) {
  qcs_program_fdump(&program->program, file);
}

void qcs_qasm_program_destroy(QCS_QasmProgram *program) {
  qcs_program_destroy(&program->program);
  arrfree(program->qregs);
  arrfree(program->cregs);
  *program = (QCS_QasmProgram){0};
}
