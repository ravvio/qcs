#include "qcs_qasm_lexer.h"

#include <stdlib.h>
#include <string.h>

void qcs_qasm_lexer_init(QCS_QasmLexer *lexer, const char *source) {
  *lexer = (QCS_QasmLexer){
      .cursor = source,
      .line = 1,
      .column = 1,
  };
  return;
}

static bool is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

// end of the number at p: digits, fraction, exponent
static const char *scan_number(const char *p, bool *real) {
  *real = false;
  while (is_digit(*p))
    p++;
  if (*p == '.') {
    *real = true;
    p++;
    while (is_digit(*p))
      p++;
  }
  if (*p == 'e' || *p == 'E') {
    const char *e = p + 1;
    if (*e == '+' || *e == '-')
      e++;
    if (is_digit(*e)) {
      *real = true;
      p = e;
      while (is_digit(*p))
        p++;
    }
  }
  return p;
}

static QCS_QasmTokenKind single_char_kind(char c) {
  switch (c) {
  case ';':
    return QCS_QASM_TOKEN_SEMICOLON;
  case ',':
    return QCS_QASM_TOKEN_COMMA;
  case '(':
    return QCS_QASM_TOKEN_ROUND_BRACKET_OPEN;
  case ')':
    return QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE;
  case '[':
    return QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN;
  case ']':
    return QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE;
  case '{':
    return QCS_QASM_TOKEN_CURLY_BRACKET_OPEN;
  case '}':
    return QCS_QASM_TOKEN_CURLY_BRACKET_CLOSE;
  case '+':
    return QCS_QASM_TOKEN_PLUS;
  case '-':
    return QCS_QASM_TOKEN_MINUS;
  case '*':
    return QCS_QASM_TOKEN_ASTERISK;
  case '/':
    return QCS_QASM_TOKEN_SLASH;
  case '^':
    return QCS_QASM_TOKEN_CARET;
  default:
    return QCS_QASM_TOKEN_ERROR;
  }
}

static void set_error(QCS_QasmToken *token, const char *message) {
  token->kind = QCS_QASM_TOKEN_ERROR;
  token->start = message;
  token->length = (uint32_t)strlen(message);
}

bool qcs_qasm_token_text_is(const QCS_QasmToken *token, const char *str) {
  return token->length == strlen(str) &&
         memcmp(token->start, str, token->length) == 0;
}

bool qcs_qasm_token_iden_is(const QCS_QasmToken *token, const char *str) {
  return token->kind == QCS_QASM_TOKEN_IDENT &&
         qcs_qasm_token_text_is(token, str);
}

QCS_QasmToken qcs_qasm_lexer_next(QCS_QasmLexer *lexer) {
  const char *p = lexer->cursor;

  // skip whitespace and comments
  for (;;) {
    if (*p == '\n') {
      lexer->line++;
      lexer->column = 1;
      p++;
    } else if (*p == ' ' || *p == '\t' || *p == '\r') {
      lexer->column++;
      p++;
    } else if (p[0] == '/' && p[1] == '/') {
      while (*p != '\n' && *p != '\0')
        p++;
    } else {
      break;
    }
  }

  QCS_QasmToken token = {
      .start = p, .line = lexer->line, .column = lexer->column};
  const char *end = p + 1;

  if (*p == '\0') {
    token.kind = QCS_QASM_TOKEN_EOF;
    end = p;
  } else if (is_ident_start(*p)) {
    token.kind = QCS_QASM_TOKEN_IDENT;
    while (is_ident_start(*end) || is_digit(*end))
      end++;
  } else if (is_digit(*p) || (*p == '.' && is_digit(p[1]))) {
    bool real;
    end = scan_number(p, &real);
    token.kind = real ? QCS_QASM_TOKEN_REAL : QCS_QASM_TOKEN_INT;
    // strtod on a NUL-terminated copy of the number
    char text[64];
    size_t length = (size_t)(end - p);
    if (length < sizeof text) {
      memcpy(text, p, length);
      text[length] = '\0';
      token.number = strtod(text, nullptr);
    } else {
      set_error(&token, "number too long");
    }
  } else if (*p == '"') {
    while (*end != '"' && *end != '\n' && *end != '\0')
      end++;
    if (*end == '"') {
      token.kind = QCS_QASM_TOKEN_STRING;
      token.start = p + 1;
      token.length = (uint32_t)(end - token.start);
      end++;
    } else {
      set_error(&token, "unterminated string");
    }
  } else if (p[0] == '-' && p[1] == '>') {
    token.kind = QCS_QASM_TOKEN_ARROW;
    end = p + 2;
  } else if (p[0] == '=' && p[1] == '=') {
    token.kind = QCS_QASM_TOKEN_EQ_EQ;
    end = p + 2;
  } else {
    token.kind = single_char_kind(*p);
    if (token.kind == QCS_QASM_TOKEN_ERROR)
      set_error(&token, "unexpected character");
  }

  if (token.kind != QCS_QASM_TOKEN_ERROR && token.kind != QCS_QASM_TOKEN_STRING)
    token.length = (uint32_t)(end - p);
  lexer->column += (uint32_t)(end - p);
  lexer->cursor = end;
  return token;
}
