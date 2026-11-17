#include "qcs_qasm_parser.h"
#include "qcs_qasm_lexer.h"
#include <math.h>
#include <qcs/qasm.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static const QCS_QasmFunction qcs_qasm_functions[] = {
    {"sin", sin},     {"cos", cos},      {"tan", tan},     {"exp", exp},
    {"sqrt", sqrt},   {"log", log},      {"arccos", acos}, {"arcsin", asin},
    {"arctan", atan}, {"ceiling", ceil}, {"floor", floor},
};

const QCS_QasmFunction *qcs_qasm_token_function(const QCS_QasmToken *token) {
  for (size_t i = 0; i < sizeof qcs_qasm_functions / sizeof *qcs_qasm_functions;
       i++) {
    if (qcs_qasm_token_iden_is(token, qcs_qasm_functions[i].name)) {
      return &qcs_qasm_functions[i];
    }
  }
  return nullptr;
}

// Internal parser definition
typedef struct {
  QCS_Arena *arena;
  QCS_QasmLexer *lexer;
  uint8_t depth;            // expression nesting
  uint8_t block_depth;      // { } nesting
  QCS_QasmToken next_token; // look ahead
  QCS_QasmToken curr_token; // last consumed
  QCS_QasmProgram *out;
  QCS_QasmError error;
} QCS_QasmParser;

// error message at line:column
static void qcs_qasm_parser_set_error(QCS_QasmParser *parser, uint32_t line,
                                      uint32_t column, const char *format,
                                      va_list args) {
  parser->error.line = line;
  parser->error.column = column;
  vsnprintf(parser->error.message, sizeof parser->error.message, format, args);
}

// error at the token's first character, always false
static bool qcs_qasm_parser_set_error_at(QCS_QasmParser *parser,
                                         QCS_QasmToken token,
                                         const char *format, ...) {
  va_list args;
  va_start(args, format);
  qcs_qasm_parser_set_error(parser, token.line, token.column, format, args);
  va_end(args);
  return false;
}

// error right after the token's last character, quotes included for
// strings, always false
static bool qcs_qasm_parser_set_error_after(QCS_QasmParser *parser,
                                            QCS_QasmToken token,
                                            const char *format, ...) {
  uint32_t quotes = token.kind == QCS_QASM_TOKEN_STRING ? 2 : 0;
  va_list args;
  va_start(args, format);
  qcs_qasm_parser_set_error(parser, token.line,
                            token.column + token.length + quotes, format, args);
  va_end(args);
  return false;
}

// next token; a lexer error becomes the parse error
static bool qcs_qasm_parser_advance(QCS_QasmParser *parser) {
  QCS_QasmToken next = qcs_qasm_lexer_next(parser->lexer);
  if (next.kind == QCS_QASM_TOKEN_ERROR) {
    return qcs_qasm_parser_set_error_at(parser, next, "%.*s", (int)next.length,
                                        next.start);
  }
  parser->curr_token = parser->next_token;
  parser->next_token = next;
  return true;
}

// advance past a token of kind, or "expected <what>"
static bool qcs_qasm_parser_expect(QCS_QasmParser *parser,
                                   QCS_QasmTokenKind kind, const char *what) {
  if (parser->next_token.kind != kind) {
    return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                           "expected %s", what);
  }
  return qcs_qasm_parser_advance(parser);
}

static bool qcs_qasm_parser_parse_expression(QCS_QasmParser *parser,
                                             QCS_QasmExpr **out);
static bool qcs_qasm_parser_parse_unary(QCS_QasmParser *parser,
                                        QCS_QasmExpr **out);

// primary := INT | REAL | name | function '(' expression ')'
//          | '(' expression ')'
static bool qcs_qasm_parser_parse_primary(QCS_QasmParser *parser,
                                          QCS_QasmExpr **out) {
  QCS_QasmToken token = parser->next_token;
  switch (token.kind) {
  case QCS_QASM_TOKEN_INT:
    if (token.integer > INT64_MAX) {
      return qcs_qasm_parser_set_error_at(parser, token, "integer too large");
    }
    *out = qcs_qasm_expr_int(parser->arena, token, (int64_t)token.integer);
    break;
  case QCS_QASM_TOKEN_REAL:
    *out = qcs_qasm_expr_float(parser->arena, token, token.number);
    break;
  case QCS_QASM_TOKEN_IDENT: {
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
    const QCS_QasmFunction *function = qcs_qasm_token_function(&token);
    if (!function) {
      if (parser->next_token.kind == QCS_QASM_TOKEN_ROUND_BRACKET_OPEN) {
        return qcs_qasm_parser_set_error_at(parser, token,
                                            "unknown function %.*s",
                                            (int)token.length, token.start);
      }
      *out = qcs_qasm_expr_name(parser->arena, token);
      return *out
                 ? true
                 : qcs_qasm_parser_set_error_at(parser, token, "out of memory");
    }
    QCS_QasmExpr *argument;
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_OPEN,
                                "'('") ||
        !qcs_qasm_parser_parse_expression(parser, &argument) ||
        !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                                "')'")) {
      return false;
    }
    *out = qcs_qasm_expr_call(parser->arena, token, argument);
    return *out ? true
                : qcs_qasm_parser_set_error_at(parser, token, "out of memory");
  }
  case QCS_QASM_TOKEN_ROUND_BRACKET_OPEN:
    // grouping only, no node of its own
    return qcs_qasm_parser_advance(parser) &&
           qcs_qasm_parser_parse_expression(parser, out) &&
           qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                                  "')'");
  case QCS_QASM_TOKEN_NOT:
  case QCS_QASM_TOKEN_TILDE:
    return qcs_qasm_parser_set_error_at(parser, token, "%.*s is not supported",
                                        (int)token.length, token.start);
  default:
    return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                           "expected expression");
  }
  // INT and REAL: consume the literal
  if (!*out) {
    return qcs_qasm_parser_set_error_at(parser, token, "out of memory");
  }
  return qcs_qasm_parser_advance(parser);
}

// power := primary ('**' unary)?
static bool qcs_qasm_parser_parse_power(QCS_QasmParser *parser,
                                        QCS_QasmExpr **out) {
  if (!qcs_qasm_parser_parse_primary(parser, out)) {
    return false;
  }
  QCS_QasmToken op = parser->next_token;
  if (op.kind != QCS_QASM_TOKEN_ASTERISK_ASTERISK) {
    return true;
  }
  QCS_QasmExpr *exponent;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_unary(parser, &exponent)) {
    return false;
  }
  *out =
      qcs_qasm_expr_binary(parser->arena, op, QCS_QASM_OP_POW, *out, exponent);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, op, "out of memory");
}

// unary := '-' unary | power
static bool qcs_qasm_parser_parse_unary(QCS_QasmParser *parser,
                                        QCS_QasmExpr **out) {
  QCS_QasmToken op = parser->next_token;
  if (parser->depth == QCS_QASM_MAX_EXPRESSION_DEPTH) {
    return qcs_qasm_parser_set_error_at(parser, op,
                                        "expression nested too deep");
  }
  parser->depth++;
  bool ok;
  if (op.kind == QCS_QASM_TOKEN_MINUS) {
    QCS_QasmExpr *operand;
    ok = qcs_qasm_parser_advance(parser) &&
         qcs_qasm_parser_parse_unary(parser, &operand);
    if (ok) {
      *out = qcs_qasm_expr_unary(parser->arena, op, QCS_QASM_OP_NEG, operand);
      ok = *out ? true
                : qcs_qasm_parser_set_error_at(parser, op, "out of memory");
    }
  } else {
    ok = qcs_qasm_parser_parse_power(parser, out);
  }
  parser->depth--;
  return ok;
}

// multiplicative := unary (('*' | '/' | '%') unary)*
static bool qcs_qasm_parser_parse_multiplicative(QCS_QasmParser *parser,
                                                 QCS_QasmExpr **out) {
  if (!qcs_qasm_parser_parse_unary(parser, out)) {
    return false;
  }
  for (;;) {
    QCS_QasmToken op = parser->next_token;
    QCS_QasmOp kind;
    if (op.kind == QCS_QASM_TOKEN_ASTERISK) {
      kind = QCS_QASM_OP_MUL;
    } else if (op.kind == QCS_QASM_TOKEN_SLASH) {
      kind = QCS_QASM_OP_DIV;
    } else if (op.kind == QCS_QASM_TOKEN_PERCENT) {
      kind = QCS_QASM_OP_MOD;
    } else {
      return true;
    }
    QCS_QasmExpr *right;
    if (!qcs_qasm_parser_advance(parser) ||
        !qcs_qasm_parser_parse_unary(parser, &right)) {
      return false;
    }
    *out = qcs_qasm_expr_binary(parser->arena, op, kind, *out, right);
    if (!*out) {
      return qcs_qasm_parser_set_error_at(parser, op, "out of memory");
    }
  }
}

// operators of OpenQASM 3 outside the subset ('^' is bitwise xor)
static bool qcs_qasm_token_is_unsupported_operator(const QCS_QasmToken *token) {
  switch (token->kind) {
  case QCS_QASM_TOKEN_EQ_EQ:
  case QCS_QASM_TOKEN_NOT_EQ:
  case QCS_QASM_TOKEN_LESSER:
  case QCS_QASM_TOKEN_LESSER_EQ:
  case QCS_QASM_TOKEN_GREATER:
  case QCS_QASM_TOKEN_GREATER_EQ:
  case QCS_QASM_TOKEN_AMP_AMP:
  case QCS_QASM_TOKEN_PIPE_PIPE:
  case QCS_QASM_TOKEN_AMP:
  case QCS_QASM_TOKEN_PIPE:
  case QCS_QASM_TOKEN_LESSER_LESSER:
  case QCS_QASM_TOKEN_GREATER_GREATER:
  case QCS_QASM_TOKEN_CARET:
    return true;
  default:
    return false;
  }
}

// additive := multiplicative (('+' | '-') multiplicative)*
static bool qcs_qasm_parser_parse_additive(QCS_QasmParser *parser,
                                           QCS_QasmExpr **out) {
  if (!qcs_qasm_parser_parse_multiplicative(parser, out)) {
    return false;
  }
  for (;;) {
    QCS_QasmToken op = parser->next_token;
    QCS_QasmOp kind;
    if (op.kind == QCS_QASM_TOKEN_PLUS) {
      kind = QCS_QASM_OP_ADD;
    } else if (op.kind == QCS_QASM_TOKEN_MINUS) {
      kind = QCS_QASM_OP_SUB;
    } else {
      return true;
    }
    QCS_QasmExpr *right;
    if (!qcs_qasm_parser_advance(parser) ||
        !qcs_qasm_parser_parse_multiplicative(parser, &right)) {
      return false;
    }
    *out = qcs_qasm_expr_binary(parser->arena, op, kind, *out, right);
    if (!*out) {
      return qcs_qasm_parser_set_error_at(parser, op, "out of memory");
    }
  }
}

// expression := additive, then no operator outside the subset
static bool qcs_qasm_parser_parse_expression(QCS_QasmParser *parser,
                                             QCS_QasmExpr **out) {
  if (!qcs_qasm_parser_parse_additive(parser, out)) {
    return false;
  }
  QCS_QasmToken op = parser->next_token;
  if (qcs_qasm_token_is_unsupported_operator(&op)) {
    return qcs_qasm_parser_set_error_at(parser, op, "%.*s is not supported",
                                        (int)op.length, op.start);
  }
  return true;
}

// OPENQASM 3; or OPENQASM 3.x;
static bool qcs_qasm_parser_parse_header(QCS_QasmParser *parser) {
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  QCS_QasmToken version = parser->next_token;
  if (version.kind != QCS_QASM_TOKEN_INT &&
      version.kind != QCS_QASM_TOKEN_REAL) {
    return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                           "expected version");
  }
  bool three = version.kind == QCS_QASM_TOKEN_INT
                   ? version.integer == 3
                   : version.number >= 3 && version.number < 4;
  if (!three) {
    return qcs_qasm_parser_set_error_at(parser, version,
                                        "only OpenQASM 3 is supported");
  }
  return qcs_qasm_parser_advance(parser) &&
         qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'");
}

// include "stdgates.inc";
static bool qcs_qasm_parser_parse_include(QCS_QasmParser *parser,
                                          QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_STRING, "file name")) {
    return false;
  }
  QCS_QasmToken file = parser->curr_token;
  if (!qcs_qasm_token_text_is(&file, "stdgates.inc")) {
    return qcs_qasm_parser_set_error_at(parser, file,
                                        "only stdgates.inc is built in");
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_include(parser->arena, at, file);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// '[' expression ']'
static bool qcs_qasm_parser_parse_brackets(QCS_QasmParser *parser,
                                           QCS_QasmExpr **out) {
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                                "'['") &&
         qcs_qasm_parser_parse_expression(parser, out) &&
         qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                                "']'");
}

// qubit[n] q; qubit q; bit[n] c; bit c; qreg q[n]; creg c[n];
static bool qcs_qasm_parser_parse_declare(QCS_QasmParser *parser,
                                          QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  bool quantum = qcs_qasm_token_iden_is(&at, "qubit") ||
                 qcs_qasm_token_iden_is(&at, "qreg");
  bool old_style = qcs_qasm_token_iden_is(&at, "qreg") ||
                   qcs_qasm_token_iden_is(&at, "creg");
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  // size before the name, or after it for qreg and creg
  QCS_QasmExpr *size = nullptr;
  if (!old_style &&
      parser->next_token.kind == QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN &&
      !qcs_qasm_parser_parse_brackets(parser, &size)) {
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "register name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  if (old_style &&
      parser->next_token.kind == QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN &&
      !qcs_qasm_parser_parse_brackets(parser, &size)) {
    return false;
  }
  if (parser->next_token.kind == QCS_QASM_TOKEN_EQ) {
    return qcs_qasm_parser_set_error_at(parser, parser->next_token,
                                        "initializers are not supported");
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_declare(parser->arena, at, quantum, name, size);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

static const struct {
  const char *name;
  QCS_QasmType type;
} qcs_qasm_types[] = {
    {"int", QCS_QASM_TYPE_INT},
    {"uint", QCS_QASM_TYPE_UINT},
    {"float", QCS_QASM_TYPE_FLOAT},
    {"angle", QCS_QASM_TYPE_ANGLE},
};

// token names a type of the subset
static bool qcs_qasm_token_is_type(const QCS_QasmToken *token) {
  for (size_t i = 0; i < sizeof qcs_qasm_types / sizeof *qcs_qasm_types; i++) {
    if (qcs_qasm_token_iden_is(token, qcs_qasm_types[i].name)) {
      return true;
    }
  }
  return false;
}

// int, uint, float or angle, with an ignored width: int[32]
static bool qcs_qasm_parser_parse_type(QCS_QasmParser *parser,
                                       QCS_QasmType *type) {
  for (size_t i = 0; i < sizeof qcs_qasm_types / sizeof *qcs_qasm_types; i++) {
    if (qcs_qasm_token_iden_is(&parser->next_token, qcs_qasm_types[i].name)) {
      *type = qcs_qasm_types[i].type;
      QCS_QasmExpr *width;
      return qcs_qasm_parser_advance(parser) &&
             (parser->next_token.kind != QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN ||
              qcs_qasm_parser_parse_brackets(parser, &width));
    }
  }
  return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                         "expected int, uint, float or angle");
}

// const type name = expression;
static bool qcs_qasm_parser_parse_const(QCS_QasmParser *parser,
                                        QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmType type;
  QCS_QasmExpr *value;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_type(parser, &type) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_EQ, "'='") ||
      !qcs_qasm_parser_parse_expression(parser, &value) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_const(parser->arena, at, type, name, value);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// input type name;
static bool qcs_qasm_parser_parse_input(QCS_QasmParser *parser,
                                        QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmType type;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_type(parser, &type) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_input(parser->arena, at, type, name);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// rest of an operand after its name: nothing, [i], [a:b] or [a:s:b];
// indexing only where indexable
static bool qcs_qasm_parser_parse_operand_after(QCS_QasmParser *parser,
                                                QCS_QasmToken name,
                                                bool indexable,
                                                QCS_QasmOperand **out) {
  if (parser->next_token.kind != QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN) {
    *out = qcs_qasm_operand(parser->arena, name);
    return *out ? true
                : qcs_qasm_parser_set_error_at(parser, name, "out of memory");
  }
  if (!indexable) {
    return qcs_qasm_parser_set_error_at(parser, parser->next_token,
                                        "gate arguments can't be indexed");
  }
  QCS_QasmExpr *first;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_expression(parser, &first)) {
    return false;
  }
  if (parser->next_token.kind != QCS_QASM_TOKEN_COLON) {
    *out = qcs_qasm_operand_index(parser->arena, name, first);
  } else {
    // a:b, or a:s:b with the step in the middle
    QCS_QasmRange range = {.start = first};
    if (!qcs_qasm_parser_advance(parser) ||
        !qcs_qasm_parser_parse_expression(parser, &range.end)) {
      return false;
    }
    if (parser->next_token.kind == QCS_QASM_TOKEN_COLON) {
      range.step = range.end;
      if (!qcs_qasm_parser_advance(parser) ||
          !qcs_qasm_parser_parse_expression(parser, &range.end)) {
        return false;
      }
    }
    *out = qcs_qasm_operand_range(parser->arena, name, range);
  }
  if (!*out) {
    return qcs_qasm_parser_set_error_at(parser, name, "out of memory");
  }
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                                "']'");
}

static bool qcs_qasm_parser_parse_operand(QCS_QasmParser *parser,
                                          bool indexable,
                                          QCS_QasmOperand **out) {
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "operand") &&
         qcs_qasm_parser_parse_operand_after(parser, parser->curr_token,
                                             indexable, out);
}

// operands separated by ',' before the ';', none allowed
static bool qcs_qasm_parser_parse_operands(QCS_QasmParser *parser,
                                           bool indexable,
                                           QCS_QasmOperand **out) {
  *out = nullptr;
  if (parser->next_token.kind == QCS_QASM_TOKEN_SEMICOLON) {
    return true;
  }
  for (QCS_QasmOperand **tail = out;; tail = &(*tail)->next) {
    if (!qcs_qasm_parser_parse_operand(parser, indexable, tail)) {
      return false;
    }
    if (parser->next_token.kind != QCS_QASM_TOKEN_COMMA) {
      return true;
    }
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
  }
}

// modifier named by token, false if none
static bool qcs_qasm_token_modifier(const QCS_QasmToken *token,
                                    QCS_QasmModifierKind *kind) {
  static const struct {
    const char *name;
    QCS_QasmModifierKind kind;
  } modifiers[] = {
      {"ctrl", QCS_QASM_MODIFIER_CTRL},
      {"negctrl", QCS_QASM_MODIFIER_NEGCTRL},
      {"inv", QCS_QASM_MODIFIER_INV},
      {"pow", QCS_QASM_MODIFIER_POW},
  };
  for (size_t i = 0; i < sizeof modifiers / sizeof *modifiers; i++) {
    if (qcs_qasm_token_iden_is(token, modifiers[i].name)) {
      *kind = modifiers[i].kind;
      return true;
    }
  }
  return false;
}

// ctrl @, ctrl(n) @, negctrl @, negctrl(n) @, inv @, pow(k) @, in order
static bool qcs_qasm_parser_parse_modifiers(QCS_QasmParser *parser,
                                            QCS_QasmModifier **out) {
  *out = nullptr;
  QCS_QasmModifier **tail = out;
  QCS_QasmModifierKind kind;
  while (qcs_qasm_token_modifier(&parser->next_token, &kind)) {
    QCS_QasmToken at = parser->next_token;
    QCS_QasmExpr *argument = nullptr;
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
    if (parser->next_token.kind == QCS_QASM_TOKEN_ROUND_BRACKET_OPEN) {
      if (kind == QCS_QASM_MODIFIER_INV) {
        return qcs_qasm_parser_set_error_at(parser, parser->next_token,
                                            "inv takes no argument");
      }
      if (!qcs_qasm_parser_advance(parser) ||
          !qcs_qasm_parser_parse_expression(parser, &argument) ||
          !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                                  "')'")) {
        return false;
      }
    } else if (kind == QCS_QASM_MODIFIER_POW) {
      return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                             "expected '('");
    }
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_AT, "'@'")) {
      return false;
    }
    *tail = qcs_qasm_modifier(parser->arena, at, kind, argument);
    if (!*tail) {
      return qcs_qasm_parser_set_error_at(parser, at, "out of memory");
    }
    tail = &(*tail)->next;
  }
  return true;
}

// rest of a gate call after its name: ('(' params ')')? operands ';'
static bool qcs_qasm_parser_parse_call_after(QCS_QasmParser *parser,
                                             QCS_QasmToken at,
                                             QCS_QasmModifier *modifiers,
                                             QCS_QasmToken name, bool indexable,
                                             QCS_QasmStmt **out) {
  QCS_QasmExpr *params = nullptr;
  if (parser->next_token.kind == QCS_QASM_TOKEN_ROUND_BRACKET_OPEN) {
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
    // expressions separated by ',', none allowed
    QCS_QasmExpr **tail = &params;
    while (parser->next_token.kind != QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE) {
      if (tail != &params &&
          !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_COMMA, "',' or ')'")) {
        return false;
      }
      if (!qcs_qasm_parser_parse_expression(parser, tail)) {
        return false;
      }
      tail = &(*tail)->next;
    }
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
  }
  QCS_QasmOperand *operands;
  if (!qcs_qasm_parser_parse_operands(parser, indexable, &operands) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_gate_call(parser->arena, at, modifiers, name, params,
                                 operands);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// modifiers, then a gate call
static bool qcs_qasm_parser_parse_modified_call(QCS_QasmParser *parser,
                                                bool indexable,
                                                QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmModifier *modifiers;
  return qcs_qasm_parser_parse_modifiers(parser, &modifiers) &&
         qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "gate name") &&
         qcs_qasm_parser_parse_call_after(parser, at, modifiers,
                                          parser->curr_token, indexable, out);
}

// bit = measure qubit; after the bit's name
static bool qcs_qasm_parser_parse_measure_assign(QCS_QasmParser *parser,
                                                 QCS_QasmToken name,
                                                 QCS_QasmStmt **out) {
  QCS_QasmOperand *bit, *qubit;
  if (!qcs_qasm_parser_parse_operand_after(parser, name, true, &bit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_EQ, "'='")) {
    return false;
  }
  if (!qcs_qasm_token_iden_is(&parser->next_token, "measure")) {
    return qcs_qasm_parser_set_error_at(parser, parser->next_token,
                                        "only measure results can be assigned");
  }
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_operand(parser, true, &qubit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_measure(parser->arena, name, qubit, bit);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, name, "out of memory");
}

// measure qubit -> bit;
static bool qcs_qasm_parser_parse_measure_arrow(QCS_QasmParser *parser,
                                                QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmOperand *qubit, *bit;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_operand(parser, true, &qubit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ARROW, "'->'") ||
      !qcs_qasm_parser_parse_operand(parser, true, &bit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_measure(parser->arena, at, qubit, bit);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// barrier operands;
static bool qcs_qasm_parser_parse_barrier(QCS_QasmParser *parser,
                                          bool indexable, QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmOperand *operands;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_operands(parser, indexable, &operands) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_barrier(parser->arena, at, operands);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// reset qubits;
static bool qcs_qasm_parser_parse_reset(QCS_QasmParser *parser,
                                        QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  QCS_QasmOperand *qubits;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_operand(parser, true, &qubits) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  *out = qcs_qasm_stmt_reset(parser->arena, at, qubits);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// Where a statement is: each allows a different set
typedef enum : uint8_t {
  QCS_QASM_CONTEXT_PROGRAM = 1 << 0,
  QCS_QASM_CONTEXT_LOOP = 1 << 1,
  QCS_QASM_CONTEXT_GATE = 1 << 2,
  QCS_QASM_CONTEXT_IF = 1 << 3,
} QCS_QasmContext;

static bool qcs_qasm_parser_parse_statement(QCS_QasmParser *parser,
                                            QCS_QasmContext context,
                                            QCS_QasmStmt **out);
static bool qcs_qasm_parser_parse_block(QCS_QasmParser *parser,
                                        QCS_QasmContext context,
                                        QCS_QasmStmt **out);

// a block, or a single statement, one level deeper
static bool qcs_qasm_parser_parse_body(QCS_QasmParser *parser, QCS_QasmToken at,
                                       QCS_QasmContext context,
                                       QCS_QasmStmt **out) {
  if (parser->block_depth == QCS_QASM_MAX_BLOCK_DEPTH) {
    return qcs_qasm_parser_set_error_at(parser, at, "blocks nested too deep");
  }
  parser->block_depth++;
  bool ok = parser->next_token.kind == QCS_QASM_TOKEN_CURLY_BRACKET_OPEN
                ? qcs_qasm_parser_parse_block(parser, context, out)
                : qcs_qasm_parser_parse_statement(parser, context, out);
  parser->block_depth--;
  return ok;
}

// '{' statements '}' in context
static bool qcs_qasm_parser_parse_block(QCS_QasmParser *parser,
                                        QCS_QasmContext context,
                                        QCS_QasmStmt **out) {
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_CURLY_BRACKET_OPEN,
                              "'{'")) {
    return false;
  }
  *out = nullptr;
  for (QCS_QasmStmt **tail = out;
       parser->next_token.kind != QCS_QASM_TOKEN_CURLY_BRACKET_CLOSE;
       tail = &(*tail)->next) {
    if (parser->next_token.kind == QCS_QASM_TOKEN_EOF) {
      return qcs_qasm_parser_set_error_after(parser, parser->curr_token,
                                             "expected '}'");
    }
    if (!qcs_qasm_parser_parse_statement(parser, context, tail)) {
      return false;
    }
  }
  return qcs_qasm_parser_advance(parser);
}

// names separated by ',', at least one
static bool qcs_qasm_parser_parse_names(QCS_QasmParser *parser,
                                        const char *what, QCS_QasmName **out) {
  for (QCS_QasmName **tail = out;; tail = &(*tail)->next) {
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, what)) {
      return false;
    }
    *tail = qcs_qasm_name(parser->arena, parser->curr_token);
    if (!*tail) {
      return qcs_qasm_parser_set_error_at(parser, parser->curr_token,
                                          "out of memory");
    }
    if (parser->next_token.kind != QCS_QASM_TOKEN_COMMA) {
      return true;
    }
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
  }
}

// gate name(params) args { body }, params optional
static bool qcs_qasm_parser_parse_gate_def(QCS_QasmParser *parser,
                                           QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "gate name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  QCS_QasmName *params = nullptr, *args;
  if (parser->next_token.kind == QCS_QASM_TOKEN_ROUND_BRACKET_OPEN) {
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
    if (parser->next_token.kind != QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE &&
        !qcs_qasm_parser_parse_names(parser, "parameter name", &params)) {
      return false;
    }
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                                "')'")) {
      return false;
    }
  }
  QCS_QasmStmt *body;
  if (!qcs_qasm_parser_parse_names(parser, "gate argument", &args) ||
      !qcs_qasm_parser_parse_block(parser, QCS_QASM_CONTEXT_GATE, &body)) {
    return false;
  }
  *out = qcs_qasm_stmt_gate_def(parser->arena, at, name, params, args, body);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// for type name in [a:b] / [a:s:b] / {set} body, type optional (int)
static bool qcs_qasm_parser_parse_for(QCS_QasmParser *parser,
                                      QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  // a type, or none (int): type names are reserved, so never the variable
  QCS_QasmType type = QCS_QASM_TYPE_INT;
  QCS_QasmToken type_token = parser->next_token;
  if (qcs_qasm_token_is_type(&type_token)) {
    if (!qcs_qasm_parser_parse_type(parser, &type)) {
      return false;
    }
    if (type != QCS_QASM_TYPE_INT && type != QCS_QASM_TYPE_UINT) {
      return qcs_qasm_parser_set_error_at(parser, type_token,
                                          "loop variables are int or uint");
    }
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "loop variable")) {
    return false;
  }
  QCS_QasmToken variable = parser->curr_token;
  if (!qcs_qasm_token_iden_is(&parser->next_token, "in")) {
    return qcs_qasm_parser_set_error_after(parser, variable, "expected in");
  }
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }

  QCS_QasmRange range = {0};
  QCS_QasmExpr *set = nullptr;
  bool is_set = parser->next_token.kind == QCS_QASM_TOKEN_CURLY_BRACKET_OPEN;
  if (is_set) {
    // {a, b, ...}
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
    for (QCS_QasmExpr **tail = &set;; tail = &(*tail)->next) {
      if (!qcs_qasm_parser_parse_expression(parser, tail)) {
        return false;
      }
      if (parser->next_token.kind != QCS_QASM_TOKEN_COMMA) {
        break;
      }
      if (!qcs_qasm_parser_advance(parser)) {
        return false;
      }
    }
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_CURLY_BRACKET_CLOSE,
                                "'}'")) {
      return false;
    }
  } else {
    // [a:b] or [a:s:b]
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                                "'[' or '{'") ||
        !qcs_qasm_parser_parse_expression(parser, &range.start) ||
        !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_COLON, "':'") ||
        !qcs_qasm_parser_parse_expression(parser, &range.end)) {
      return false;
    }
    if (parser->next_token.kind == QCS_QASM_TOKEN_COLON) {
      range.step = range.end;
      if (!qcs_qasm_parser_advance(parser) ||
          !qcs_qasm_parser_parse_expression(parser, &range.end)) {
        return false;
      }
    }
    if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                                "']'")) {
      return false;
    }
  }

  // the body
  QCS_QasmStmt *body;
  if (!qcs_qasm_parser_parse_body(parser, at, QCS_QASM_CONTEXT_LOOP, &body)) {
    return false;
  }
  *out = is_set ? qcs_qasm_stmt_for_set(parser->arena, at, type, variable, set,
                                        body)
                : qcs_qasm_stmt_for_range(parser->arena, at, type, variable,
                                          range, body);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// if (condition) body, then else body if any; condition: bit, !bit or
// bits == value
static bool qcs_qasm_parser_parse_if(QCS_QasmParser *parser,
                                     QCS_QasmStmt **out) {
  QCS_QasmToken at = parser->next_token;
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_OPEN,
                              "'('")) {
    return false;
  }

  // the condition
  bool negate = parser->next_token.kind == QCS_QASM_TOKEN_NOT;
  if (negate && !qcs_qasm_parser_advance(parser)) {
    return false;
  }
  QCS_QasmOperand *bits;
  QCS_QasmExpr *value = nullptr;
  if (!qcs_qasm_parser_parse_operand(parser, true, &bits)) {
    return false;
  }
  if (!negate && parser->next_token.kind == QCS_QASM_TOKEN_EQ_EQ &&
      (!qcs_qasm_parser_advance(parser) ||
       !qcs_qasm_parser_parse_expression(parser, &value))) {
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                              "')'")) {
    return false;
  }

  // the body, then the else body
  QCS_QasmStmt *body, *else_body = nullptr;
  if (!qcs_qasm_parser_parse_body(parser, at, QCS_QASM_CONTEXT_IF, &body)) {
    return false;
  }
  if (qcs_qasm_token_iden_is(&parser->next_token, "else")) {
    QCS_QasmToken else_token = parser->next_token;
    if (!qcs_qasm_parser_advance(parser) ||
        !qcs_qasm_parser_parse_body(parser, else_token, QCS_QASM_CONTEXT_IF,
                                    &else_body)) {
      return false;
    }
  }
  *out =
      qcs_qasm_stmt_if(parser->arena, at, bits, negate, value, body, else_body);
  return *out ? true
              : qcs_qasm_parser_set_error_at(parser, at, "out of memory");
}

// OpenQASM 3 keywords that start a statement outside the subset
static const char *qcs_qasm_unsupported[] = {
    "while", "switch", "case",    "default",       "break",    "continue",
    "end",   "return", "def",     "extern",        "let",      "box",
    "delay", "cal",    "defcal",  "defcalgrammar", "duration", "stretch",
    "array", "bool",   "complex", "output",        "readonly", "mutable",
    "void",  "opaque",
};

// the place a context names, for errors
static const char *qcs_qasm_context_name(QCS_QasmContext context) {
  switch (context) {
  case QCS_QASM_CONTEXT_PROGRAM:
    return "at the top level";
  case QCS_QASM_CONTEXT_LOOP:
    return "in a loop";
  case QCS_QASM_CONTEXT_GATE:
    return "in a gate body";
  case QCS_QASM_CONTEXT_IF:
    return "in an if";
  }
  return "here";
}

// the statement named by token is allowed in context, else an error
static bool qcs_qasm_parser_allow(QCS_QasmParser *parser, QCS_QasmToken token,
                                  const char *what, uint8_t contexts,
                                  QCS_QasmContext context) {
  if (contexts & context) {
    return true;
  }
  return qcs_qasm_parser_set_error_at(parser, token, "%s is not allowed %s",
                                      what, qcs_qasm_context_name(context));
}

// one statement in context, chosen by its first token
static bool qcs_qasm_parser_parse_statement(QCS_QasmParser *parser,
                                            QCS_QasmContext context,
                                            QCS_QasmStmt **out) {
  const uint8_t program = QCS_QASM_CONTEXT_PROGRAM;
  const uint8_t loops = program | QCS_QASM_CONTEXT_LOOP;
  const uint8_t ifs = loops | QCS_QASM_CONTEXT_IF;
  bool indexable = context != QCS_QASM_CONTEXT_GATE;

  QCS_QasmToken token = parser->next_token;
  if (token.kind == QCS_QASM_TOKEN_AT) {
    return qcs_qasm_parser_set_error_at(parser, token,
                                        "annotations are not supported");
  }
  if (token.kind != QCS_QASM_TOKEN_IDENT) {
    return qcs_qasm_parser_set_error_at(parser, token, "expected statement");
  }
  for (size_t i = 0;
       i < sizeof qcs_qasm_unsupported / sizeof *qcs_qasm_unsupported; i++) {
    if (qcs_qasm_token_iden_is(&token, qcs_qasm_unsupported[i])) {
      return qcs_qasm_parser_set_error_at(parser, token, "%s is not supported",
                                          qcs_qasm_unsupported[i]);
    }
  }
  if (qcs_qasm_token_is_type(&token)) {
    return qcs_qasm_parser_set_error_at(parser, token,
                                        "variables are not supported, only "
                                        "const and input");
  }
  if (qcs_qasm_token_iden_is(&token, "OPENQASM")) {
    return qcs_qasm_parser_set_error_at(parser, token,
                                        "OPENQASM must come first");
  }

  if (qcs_qasm_token_iden_is(&token, "include")) {
    return qcs_qasm_parser_allow(parser, token, "include", program, context) &&
           qcs_qasm_parser_parse_include(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "qubit") ||
      qcs_qasm_token_iden_is(&token, "bit") ||
      qcs_qasm_token_iden_is(&token, "qreg") ||
      qcs_qasm_token_iden_is(&token, "creg")) {
    return qcs_qasm_parser_allow(parser, token, "a declaration", program,
                                 context) &&
           qcs_qasm_parser_parse_declare(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "const")) {
    return qcs_qasm_parser_allow(parser, token, "const", program, context) &&
           qcs_qasm_parser_parse_const(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "input")) {
    return qcs_qasm_parser_allow(parser, token, "input", program, context) &&
           qcs_qasm_parser_parse_input(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "gate")) {
    return qcs_qasm_parser_allow(parser, token, "gate", program, context) &&
           qcs_qasm_parser_parse_gate_def(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "for")) {
    return qcs_qasm_parser_allow(parser, token, "for", loops, context) &&
           qcs_qasm_parser_parse_for(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "if")) {
    return qcs_qasm_parser_allow(parser, token, "if", ifs, context) &&
           qcs_qasm_parser_parse_if(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "else")) {
    return qcs_qasm_parser_set_error_at(parser, token, "else without if");
  }
  if (qcs_qasm_token_iden_is(&token, "measure")) {
    return qcs_qasm_parser_allow(parser, token, "measure", ifs, context) &&
           qcs_qasm_parser_parse_measure_arrow(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "reset")) {
    return qcs_qasm_parser_allow(parser, token, "reset", ifs, context) &&
           qcs_qasm_parser_parse_reset(parser, out);
  }
  if (qcs_qasm_token_iden_is(&token, "barrier")) {
    return qcs_qasm_parser_parse_barrier(parser, indexable, out);
  }
  QCS_QasmModifierKind modifier;
  if (qcs_qasm_token_modifier(&token, &modifier)) {
    return qcs_qasm_parser_parse_modified_call(parser, indexable, out);
  }

  // a gate call, or a measure assignment: '=' or '[' after the name
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  QCS_QasmTokenKind after = parser->next_token.kind;
  if (after == QCS_QASM_TOKEN_EQ ||
      after == QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN) {
    return qcs_qasm_parser_allow(parser, token, "measure", ifs, context) &&
           qcs_qasm_parser_parse_measure_assign(parser, token, out);
  }
  return qcs_qasm_parser_parse_call_after(parser, token, nullptr, token,
                                          indexable, out);
}

// parser over source, before its first token
static QCS_QasmParser qcs_qasm_parser_create(QCS_QasmLexer *lexer,
                                             const char *source,
                                             QCS_Arena *arena) {
  qcs_qasm_lexer_init(lexer, source);
  return (QCS_QasmParser){
      .arena = arena,
      .lexer = lexer,
      // empty token at 1:1, the current one after the first advance, so
      // errors after it land at the start
      .next_token = {.line = 1, .column = 1},
  };
}

bool qcs_qasm_parse_program(const char *source, QCS_Arena *arena,
                            QCS_QasmStmt **out, QCS_QasmError *error) {
  QCS_QasmLexer lexer;
  QCS_QasmParser parser = qcs_qasm_parser_create(&lexer, source, arena);
  *out = nullptr;
  // optional header, then statements to the end
  bool ok = qcs_qasm_parser_advance(&parser);
  if (ok && qcs_qasm_token_iden_is(&parser.next_token, "OPENQASM")) {
    ok = qcs_qasm_parser_parse_header(&parser);
  }
  for (QCS_QasmStmt **tail = out;
       ok && parser.next_token.kind != QCS_QASM_TOKEN_EOF;
       tail = &(*tail)->next) {
    ok = qcs_qasm_parser_parse_statement(&parser, QCS_QASM_CONTEXT_PROGRAM,
                                         tail);
  }
  if (!ok) {
    *error = parser.error;
  }
  return ok;
}

bool qcs_qasm_parse_expression(const char *source, QCS_Arena *arena,
                               QCS_QasmExpr **out, QCS_QasmError *error) {
  QCS_QasmLexer lexer;
  QCS_QasmParser parser = qcs_qasm_parser_create(&lexer, source, arena);
  // first token, the expression, then nothing else
  bool ok = qcs_qasm_parser_advance(&parser) &&
            qcs_qasm_parser_parse_expression(&parser, out);
  if (ok && parser.next_token.kind != QCS_QASM_TOKEN_EOF) {
    QCS_QasmToken extra = parser.next_token;
    ok = qcs_qasm_parser_set_error_at(&parser, extra, "unexpected %.*s",
                                      (int)extra.length, extra.start);
  }
  if (!ok) {
    *error = parser.error;
  }
  return ok;
}
