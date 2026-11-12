// OpenQASM 2.0 (https://arxiv.org/abs/1707.03429) lexer
#ifndef QCS_QASM_LEXER_DECL
#define QCS_QASM_LEXER_DECL

#include <stdint.h>

typedef enum : uint8_t {
  QCS_QASM_TOKEN_EOF,
  QCS_QASM_TOKEN_ERROR,
  QCS_QASM_TOKEN_IDENT,
  QCS_QASM_TOKEN_INT,
  QCS_QASM_TOKEN_REAL,
  QCS_QASM_TOKEN_STRING,
  QCS_QASM_TOKEN_ARROW,
  QCS_QASM_TOKEN_EQ_EQ,
  // Single char tokens
  QCS_QASM_TOKEN_SEMICOLON,
  QCS_QASM_TOKEN_COMMA,
  QCS_QASM_TOKEN_ROUND_BRACKET_OPEN,
  QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
  QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
  QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
  QCS_QASM_TOKEN_CURLY_BRACKET_OPEN,
  QCS_QASM_TOKEN_CURLY_BRACKET_CLOSE,
  QCS_QASM_TOKEN_PLUS,
  QCS_QASM_TOKEN_MINUS,
  QCS_QASM_TOKEN_ASTERISK,
  QCS_QASM_TOKEN_SLASH,
  QCS_QASM_TOKEN_CARET,
} QCS_QasmTokenKind;

typedef struct {
  QCS_QasmTokenKind kind;
  // Token text in the source, not NUL-terminated: STRING without quotes,
  // ERROR the message
  const char *start;
  uint32_t length;
  uint32_t line, column;
  // INT or REAL
  double number;
} QCS_QasmToken;

typedef struct {
  // Current position in the caller-provided source
  const char *cursor;
  // Current line and column
  uint32_t line, column;
} QCS_QasmLexer;

void qcs_qasm_lexer_init(QCS_QasmLexer *lexer, const char *source);
QCS_QasmToken qcs_qasm_lexer_next(QCS_QasmLexer *lexer);

#endif
