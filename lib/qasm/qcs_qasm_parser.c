#include "qcs_qasm_lexer.h"
#include <qcs/qasm.h>
#include <stb_ds.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Internal parser definition
typedef struct {
  QCS_QasmLexer *lexer;
  QCS_QasmToken next_token;
  QCS_QasmToken curr_token;
  QCS_QasmProgram *out;
  QCS_QasmError error;
  bool qelib1;
} QCS_QasmParser;

static QCS_QasmError qcs_qasm_error_from_token(QCS_QasmToken token) {
  QCS_QasmError error = {
      .line = token.line,
      .column = token.column,
  };
  size_t m = QCS_QASM_ERROR_BUFFER_LENGTH - 1;
  size_t n = token.length < m ? token.length : m; // leave room for '\0'
  memcpy(error.message, token.start, n);
  error.message[n] = '\0';
  return error;
}

static void qcs_qasm_parser_error_at(QCS_QasmParser *parser, uint32_t line,
                                     uint32_t column, const char *format, ...) {
  parser->error.line = line;
  parser->error.column = column;
  va_list args;
  va_start(args, format);
  vsnprintf(parser->error.message, sizeof parser->error.message, format, args);
  va_end(args);
}

static bool qcs_qasm_parser_advance(QCS_QasmParser *parser) {
  QCS_QasmToken next = qcs_qasm_lexer_next(parser->lexer);
  if (next.kind == QCS_QASM_TOKEN_ERROR) {
    parser->error = qcs_qasm_error_from_token(next);
    return false;
  }
  parser->curr_token = parser->next_token;
  parser->next_token = next;
  return true;
}

static bool qcs_qasm_parser_accept(QCS_QasmParser *parser,
                                   QCS_QasmTokenKind kind) {
  if (parser->next_token.kind != kind) {
    return false;
  }
  return qcs_qasm_parser_advance(parser);
}

static bool qcs_qasm_parser_expect(QCS_QasmParser *parser,
                                   QCS_QasmTokenKind kind, const char *what) {
  if (parser->next_token.kind != kind) {
    // error right after the current token, quotes included for strings
    QCS_QasmToken curr = parser->curr_token;
    uint32_t quotes = curr.kind == QCS_QASM_TOKEN_STRING ? 2 : 0;
    qcs_qasm_parser_error_at(parser, curr.line,
                             curr.column + curr.length + quotes, "expected %s",
                             what);
    return false;
  }
  return qcs_qasm_parser_advance(parser);
}

// Parse qasm header `OPENQASM 2.0;`
static bool qcs_qasm_parser_parse_header(QCS_QasmParser *parser) {
  QCS_QasmToken token = parser->next_token;
  if (!qcs_qasm_token_iden_is(&token, "OPENQASM")) {
    qcs_qasm_parser_error_at(parser, token.line, token.column,
                             "expected OPENQASM");
    return false;
  }
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_REAL, "version")) {
    return false;
  }
  QCS_QasmToken version = parser->curr_token;
  if (version.number != 2.0) {
    qcs_qasm_parser_error_at(parser, version.line, version.column,
                             "only OpenQASM 2.0 is supported");
    return false;
  }
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'");
}

// Parse qasm include `include qelib1.inc`
static bool qcs_qasm_parser_parse_include(QCS_QasmParser *parser) {
  // advance past 'include'
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_STRING, "file")) {
    return false;
  }
  // check included
  QCS_QasmToken include = parser->curr_token;
  if (!qcs_qasm_token_text_is(&include, "qelib1.inc")) {
    qcs_qasm_parser_error_at(parser, include.line, include.column,
                             "only qelib1.inc is built in");
    return false;
  }
  // set qelib1.inc
  parser->qelib1 = true;
  // end
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'");
}

// Register named by token, or NULL
static QCS_QasmRegister *qcs_qasm_find_register(QCS_QasmRegister *registers,
                                                const QCS_QasmToken *token) {
  for (ptrdiff_t i = 0; i < arrlen(registers); i++) {
    if (qcs_qasm_token_text_is(token, registers[i].name)) {
      return &registers[i];
    }
  }
  return nullptr;
}

// Parse register `qreg name[size];` or `creg name[size];`
static bool qcs_qasm_parser_parse_reg(QCS_QasmParser *parser, bool quantum) {
  QCS_QasmProgram *out = parser->out;
  // advance past 'qreg' or 'creg'
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  // register name
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "register name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  if (name.length >= QCS_QASM_REGISTER_NAME_BUFFER_LENGTH) {
    qcs_qasm_parser_error_at(parser, name.line, name.column,
                             "register name longer than %u characters",
                             QCS_QASM_REGISTER_NAME_BUFFER_LENGTH - 1);
    return false;
  }
  if (qcs_qasm_find_register(out->qregs, &name) ||
      qcs_qasm_find_register(out->cregs, &name)) {
    qcs_qasm_parser_error_at(parser, name.line, name.column,
                             "register %.*s already declared", (int)name.length,
                             name.start);
    return false;
  }
  // register size
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                              "'['") ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_INT, "register size")) {
    return false;
  }
  QCS_QasmToken size = parser->curr_token;
  uint32_t *count = quantum ? &out->circuit.qubit_count : &out->clbit_count;
  if (size.number < 1) {
    qcs_qasm_parser_error_at(parser, size.line, size.column,
                             "register size must be at least 1");
    return false;
  }
  // compared as double, before the cast
  if (*count + size.number > QCS_MAX_QUBITS) {
    qcs_qasm_parser_error_at(parser, size.line, size.column, "more than %u %s",
                             QCS_MAX_QUBITS, quantum ? "qubits" : "clbits");
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                              "']'") ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  // add register after the ones already declared
  QCS_QasmRegister reg = {.offset = *count, .size = (uint32_t)size.number};
  memcpy(reg.name, name.start, name.length);
  if (quantum) {
    arrput(out->qregs, reg);
  } else {
    arrput(out->cregs, reg);
  }
  *count += reg.size;
  return true;
}

// Parse argument `name[i]` into a global qubit (or clbit) index
static bool qcs_qasm_parser_parse_argument(QCS_QasmParser *parser, bool quantum,
                                           uint32_t *index) {
  QCS_QasmProgram *out = parser->out;
  // register name
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "register name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  QCS_QasmRegister *reg =
      qcs_qasm_find_register(quantum ? out->qregs : out->cregs, &name);
  if (!reg) {
    // a register of the other kind, or none
    if (qcs_qasm_find_register(quantum ? out->cregs : out->qregs, &name)) {
      qcs_qasm_parser_error_at(parser, name.line, name.column, "%.*s is a %s",
                               (int)name.length, name.start,
                               quantum ? "creg" : "qreg");
    } else {
      qcs_qasm_parser_error_at(parser, name.line, name.column,
                               "unknown register %.*s", (int)name.length,
                               name.start);
    }
    return false;
  }
  // index
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                              "'['") ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_INT, "index")) {
    return false;
  }
  QCS_QasmToken i = parser->curr_token;
  // compared as double, before the cast
  if (i.number >= reg->size) {
    qcs_qasm_parser_error_at(parser, i.line, i.column,
                             "index out of range, %s has size %u", reg->name,
                             reg->size);
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                              "']'")) {
    return false;
  }
  *index = reg->offset + (uint32_t)i.number;
  return true;
}

// Builders from the qubits in QASM argument order
static QCS_Gate qcs_qasm_build_id(const uint32_t *q) {
  return qcs_gate_create_id(q[0]);
}
static QCS_Gate qcs_qasm_build_x(const uint32_t *q) {
  return qcs_gate_create_x(q[0]);
}
static QCS_Gate qcs_qasm_build_y(const uint32_t *q) {
  return qcs_gate_create_y(q[0]);
}
static QCS_Gate qcs_qasm_build_z(const uint32_t *q) {
  return qcs_gate_create_z(q[0]);
}
static QCS_Gate qcs_qasm_build_h(const uint32_t *q) {
  return qcs_gate_create_h(q[0]);
}
// control first in QASM, target first in qcs_gate_create_c*
static QCS_Gate qcs_qasm_build_cx(const uint32_t *q) {
  return qcs_gate_create_cx(q[1], q[0]);
}
static QCS_Gate qcs_qasm_build_cy(const uint32_t *q) {
  return qcs_gate_create_cy(q[1], q[0]);
}
static QCS_Gate qcs_qasm_build_cz(const uint32_t *q) {
  return qcs_gate_create_cz(q[1], q[0]);
}

// Gate the parser knows: name, qubit count, builder
typedef struct {
  const char *name;
  uint32_t qubit_count;
  QCS_Gate (*build)(const uint32_t *qubits);
} QCS_QasmGateDef;

#define QCS_QASM_MAX_GATE_QUBITS 2

static const QCS_QasmGateDef qcs_qasm_gates[] = {
    {"id", 1, qcs_qasm_build_id}, {"x", 1, qcs_qasm_build_x},
    {"y", 1, qcs_qasm_build_y},   {"z", 1, qcs_qasm_build_z},
    {"h", 1, qcs_qasm_build_h},   {"cx", 2, qcs_qasm_build_cx},
    {"cy", 2, qcs_qasm_build_cy}, {"cz", 2, qcs_qasm_build_cz},
};

// Gate named by token, or NULL
static const QCS_QasmGateDef *qcs_qasm_find_gate(const QCS_QasmToken *token) {
  for (size_t i = 0; i < sizeof qcs_qasm_gates / sizeof *qcs_qasm_gates; i++) {
    if (qcs_qasm_token_text_is(token, qcs_qasm_gates[i].name)) {
      return &qcs_qasm_gates[i];
    }
  }
  return nullptr;
}

// Parse gate call `name a, b;`
static bool qcs_qasm_parser_parse_gate(QCS_QasmParser *parser) {
  // gate name
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_IDENT, "gate name")) {
    return false;
  }
  QCS_QasmToken name = parser->curr_token;
  const QCS_QasmGateDef *def = qcs_qasm_find_gate(&name);
  if (!def) {
    qcs_qasm_parser_error_at(parser, name.line, name.column,
                             "unknown gate %.*s", (int)name.length, name.start);
    return false;
  }
  // arguments, separated by ','
  uint32_t qubits[QCS_QASM_MAX_GATE_QUBITS];
  uint32_t count = 0;
  while (true) {
    QCS_QasmToken arg = parser->next_token;
    if (count == def->qubit_count) {
      qcs_qasm_parser_error_at(parser, arg.line, arg.column,
                               "%s takes %u qubits", def->name,
                               def->qubit_count);
      return false;
    }
    if (!qcs_qasm_parser_parse_argument(parser, true, &qubits[count])) {
      return false;
    }
    for (uint32_t j = 0; j < count; j++) {
      if (qubits[j] == qubits[count]) {
        qcs_qasm_parser_error_at(parser, arg.line, arg.column,
                                 "same qubit twice in %s", def->name);
        return false;
      }
    }
    count++;
    if (parser->next_token.kind != QCS_QASM_TOKEN_COMMA) {
      break;
    }
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
  }
  if (count != def->qubit_count) {
    qcs_qasm_parser_error_at(parser, name.line, name.column,
                             "%s takes %u qubits, got %u", def->name,
                             def->qubit_count, count);
    return false;
  }
  if (!qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  QCS_Gate gate = def->build(qubits);
  if (qcs_circuit_push_gate(&parser->out->circuit, gate) != QCS_ERR_OK) {
    qcs_qasm_parser_error_at(parser, name.line, name.column, "invalid gate %s",
                             def->name);
    return false;
  }
  return true;
}

// Parse measurement `measure q[i] -> c[j];`
static bool qcs_qasm_parser_parse_measure(QCS_QasmParser *parser) {
  // advance past 'measure'
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  QCS_QasmMeasure measure;
  if (!qcs_qasm_parser_parse_argument(parser, true, &measure.qubit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_ARROW, "'->'") ||
      !qcs_qasm_parser_parse_argument(parser, false, &measure.clbit) ||
      !qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'")) {
    return false;
  }
  arrput(parser->out->measures, measure);
  return true;
}

// Parse barrier `barrier a, b, ...;`, checked and dropped
static bool qcs_qasm_parser_parse_barrier(QCS_QasmParser *parser) {
  // advance past 'barrier'
  if (!qcs_qasm_parser_advance(parser)) {
    return false;
  }
  // arguments, separated by ','
  while (true) {
    uint32_t qubit;
    if (!qcs_qasm_parser_parse_argument(parser, true, &qubit)) {
      return false;
    }
    if (parser->next_token.kind != QCS_QASM_TOKEN_COMMA) {
      break;
    }
    if (!qcs_qasm_parser_advance(parser)) {
      return false;
    }
  }
  return qcs_qasm_parser_expect(parser, QCS_QASM_TOKEN_SEMICOLON, "';'");
}

// Parse one statement, chosen by its first token
static bool qcs_qasm_parser_parse_statement(QCS_QasmParser *parser) {
  QCS_QasmToken token = parser->next_token;
  if (token.kind != QCS_QASM_TOKEN_IDENT) {
    qcs_qasm_parser_error_at(parser, token.line, token.column,
                             "expected statement");
    return false;
  }
  if (qcs_qasm_token_iden_is(&token, "include")) {
    return qcs_qasm_parser_parse_include(parser);
  }
  if (qcs_qasm_token_iden_is(&token, "qreg")) {
    return qcs_qasm_parser_parse_reg(parser, true);
  }
  if (qcs_qasm_token_iden_is(&token, "creg")) {
    return qcs_qasm_parser_parse_reg(parser, false);
  }
  if (qcs_qasm_token_iden_is(&token, "measure")) {
    return qcs_qasm_parser_parse_measure(parser);
  }
  if (qcs_qasm_token_iden_is(&token, "barrier")) {
    return qcs_qasm_parser_parse_barrier(parser);
  }
  // keywords of OpenQASM 2 this parser doesn't handle
  const char *unsupported[] = {"gate", "opaque", "reset", "if"};
  for (size_t i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
    if (qcs_qasm_token_iden_is(&token, unsupported[i])) {
      qcs_qasm_parser_error_at(parser, token.line, token.column,
                               "%s is not supported", unsupported[i]);
      return false;
    }
  }
  return qcs_qasm_parser_parse_gate(parser);
}

// Parse whole program: header, then statements until EOF
static bool qcs_qasm_parser_parse_program(QCS_QasmParser *parser) {
  // first token, then the header
  if (!qcs_qasm_parser_advance(parser) ||
      !qcs_qasm_parser_parse_header(parser)) {
    return false;
  }
  while (parser->next_token.kind != QCS_QASM_TOKEN_EOF) {
    if (!qcs_qasm_parser_parse_statement(parser)) {
      return false;
    }
  }
  return true;
}

QCS_Error qcs_qasm_parse(const char *source, QCS_QasmProgram *out,
                         QCS_QasmError *error) {
  // set up program
  *out = (QCS_QasmProgram){0};
  qcs_circuit_create(0, &out->circuit);
  // create lexer and parser
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, source);
  QCS_QasmParser parser = {
      .lexer = &lexer,
      .out = out,
  };
  if (!qcs_qasm_parser_parse_program(&parser)) {
    qcs_qasm_program_destroy(out);
    *error = parser.error;
    return QCS_ERR_QASM_PARSE_FAILURE;
  }
  return QCS_ERR_OK;
}

void qcs_qasm_program_destroy(QCS_QasmProgram *program) {
  qcs_circuit_destroy(&program->circuit);
  arrfree(program->qregs);
  arrfree(program->cregs);
  arrfree(program->measures);
  *program = (QCS_QasmProgram){0};
}
