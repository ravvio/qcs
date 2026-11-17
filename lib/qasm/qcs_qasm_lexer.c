#include "qcs_qasm_lexer.h"

#include <errno.h>
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

static bool qcs_qasm_char_is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool qcs_qasm_char_is_digit(char c) { return c >= '0' && c <= '9'; }

// digit of base 2, 8, 10 or 16
static bool qcs_qasm_char_is_base_digit(char c, int base) {
  if (base == 16)
    return qcs_qasm_char_is_digit(c) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  return c >= '0' && c < '0' + base;
}

// length of π, τ or ℇ (UTF-8) at p, 0 if none
static size_t qcs_qasm_text_letter_length(const char *p) {
  static const char *letters[] = {"\xcf\x80", "\xcf\x84", "\xe2\x84\x87"};
  for (size_t i = 0; i < sizeof letters / sizeof *letters; i++) {
    size_t n = strlen(letters[i]);
    if (strncmp(p, letters[i], n) == 0)
      return n;
  }
  return 0;
}

// end of the identifier at p: letters, digits, '_', π τ ℇ
static const char *qcs_qasm_text_scan_ident(const char *p) {
  for (;;) {
    size_t n = qcs_qasm_text_letter_length(p);
    if (qcs_qasm_char_is_ident_start(*p) || qcs_qasm_char_is_digit(*p))
      p++;
    else if (n > 0)
      p += n;
    else
      return p;
  }
}

// end of the digits of base at p, '_' only between two digits
static const char *qcs_qasm_text_scan_digits(const char *p, int base) {
  bool after_digit = false;
  for (;;) {
    if (qcs_qasm_char_is_base_digit(*p, base)) {
      after_digit = true;
      p++;
    } else if (*p == '_' && after_digit &&
               qcs_qasm_char_is_base_digit(p[1], base)) {
      p++;
    } else {
      return p;
    }
  }
}

// end of the number at p: 0x, 0o, 0b integers, or digits, fraction,
// exponent
static const char *qcs_qasm_text_scan_number(const char *p, int *base,
                                             bool *real) {
  *base = 10;
  *real = false;
  if (p[0] == '0') {
    char prefix = p[1];
    int b = prefix == 'x' || prefix == 'X'   ? 16
            : prefix == 'o'                  ? 8
            : prefix == 'b' || prefix == 'B' ? 2
                                             : 0;
    if (b != 0 && qcs_qasm_char_is_base_digit(p[2], b)) {
      *base = b;
      return qcs_qasm_text_scan_digits(p + 2, b);
    }
  }
  p = qcs_qasm_text_scan_digits(p, 10);
  if (*p == '.') {
    *real = true;
    p = qcs_qasm_text_scan_digits(p + 1, 10);
  }
  if (*p == 'e' || *p == 'E') {
    const char *e = p + 1;
    if (*e == '+' || *e == '-')
      e++;
    if (qcs_qasm_char_is_digit(*e)) {
      *real = true;
      p = qcs_qasm_text_scan_digits(e, 10);
    }
  }
  return p;
}

static QCS_QasmTokenKind qcs_qasm_char_token_kind(char c) {
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
  case '=':
    return QCS_QASM_TOKEN_EQ;
  case '<':
    return QCS_QASM_TOKEN_LESSER;
  case '>':
    return QCS_QASM_TOKEN_GREATER;
  case '!':
    return QCS_QASM_TOKEN_NOT;
  case '~':
    return QCS_QASM_TOKEN_TILDE;
  case '&':
    return QCS_QASM_TOKEN_AMP;
  case '|':
    return QCS_QASM_TOKEN_PIPE;
  case '%':
    return QCS_QASM_TOKEN_PERCENT;
  case ':':
    return QCS_QASM_TOKEN_COLON;
  case '@':
    return QCS_QASM_TOKEN_AT;
  default:
    return QCS_QASM_TOKEN_ERROR;
  }
}

// two-char symbols, matched before single ones
static const struct {
  char first, second;
  QCS_QasmTokenKind kind;
} qcs_qasm_two_char_symbols[] = {
    {'-', '>', QCS_QASM_TOKEN_ARROW},
    {'=', '=', QCS_QASM_TOKEN_EQ_EQ},
    {'*', '*', QCS_QASM_TOKEN_ASTERISK_ASTERISK},
    {'!', '=', QCS_QASM_TOKEN_NOT_EQ},
    {'<', '=', QCS_QASM_TOKEN_LESSER_EQ},
    {'>', '=', QCS_QASM_TOKEN_GREATER_EQ},
    {'&', '&', QCS_QASM_TOKEN_AMP_AMP},
    {'|', '|', QCS_QASM_TOKEN_PIPE_PIPE},
    {'<', '<', QCS_QASM_TOKEN_LESSER_LESSER},
    {'>', '>', QCS_QASM_TOKEN_GREATER_GREATER},
};

static void qcs_qasm_token_set_error(QCS_QasmToken *token,
                                     const char *message) {
  token->kind = QCS_QASM_TOKEN_ERROR;
  token->start = message;
  token->length = (uint32_t)strlen(message);
}

// line and column after the text from p to end, one column per character
static void qcs_qasm_lexer_move_over(QCS_QasmLexer *lexer, const char *p,
                                     const char *end) {
  for (; p < end; p++) {
    if (*p == '\n') {
      lexer->line++;
      lexer->column = 1;
    } else if (((unsigned char)*p & 0xC0) != 0x80) {
      lexer->column++;
    }
  }
}

// value of the number from p to end, without '_' and base prefix
static void qcs_qasm_token_set_number(QCS_QasmToken *token, const char *p,
                                      const char *end, int base) {
  if (base != 10)
    p += 2;
  char text[64];
  size_t length = 0;
  for (; p < end; p++) {
    if (*p == '_')
      continue;
    if (length == sizeof text - 1) {
      qcs_qasm_token_set_error(token, "number too long");
      return;
    }
    text[length++] = *p;
  }
  text[length] = '\0';
  if (token->kind == QCS_QASM_TOKEN_REAL) {
    token->number = strtod(text, nullptr);
    return;
  }
  errno = 0;
  unsigned long long value = strtoull(text, nullptr, base);
  if (errno == ERANGE) {
    qcs_qasm_token_set_error(token, "number too large");
    return;
  }
  token->integer = value;
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
    } else if (p[0] == '/' && p[1] == '*') {
      const char *close = strstr(p + 2, "*/");
      if (!close)
        break; // unterminated, an error below
      qcs_qasm_lexer_move_over(lexer, p, close + 2);
      p = close + 2;
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
  } else if (p[0] == '/' && p[1] == '*') {
    qcs_qasm_token_set_error(&token, "unterminated comment");
    end = p + strlen(p);
  } else if (qcs_qasm_char_is_ident_start(*p) ||
             qcs_qasm_text_letter_length(p) > 0) {
    token.kind = QCS_QASM_TOKEN_IDENT;
    end = qcs_qasm_text_scan_ident(p);
  } else if (qcs_qasm_char_is_digit(*p) ||
             (*p == '.' && qcs_qasm_char_is_digit(p[1]))) {
    int base;
    bool real;
    end = qcs_qasm_text_scan_number(p, &base, &real);
    token.kind = real ? QCS_QASM_TOKEN_REAL : QCS_QASM_TOKEN_INT;
    qcs_qasm_token_set_number(&token, p, end, base);
  } else if (*p == '"') {
    while (*end != '"' && *end != '\n' && *end != '\0')
      end++;
    if (*end == '"') {
      token.kind = QCS_QASM_TOKEN_STRING;
      token.start = p + 1;
      token.length = (uint32_t)(end - token.start);
      end++;
    } else {
      qcs_qasm_token_set_error(&token, "unterminated string");
    }
  } else {
    token.kind = QCS_QASM_TOKEN_ERROR;
    for (size_t i = 0; i < sizeof qcs_qasm_two_char_symbols /
                               sizeof *qcs_qasm_two_char_symbols;
         i++) {
      if (p[0] == qcs_qasm_two_char_symbols[i].first &&
          p[1] == qcs_qasm_two_char_symbols[i].second) {
        token.kind = qcs_qasm_two_char_symbols[i].kind;
        end = p + 2;
        break;
      }
    }
    if (token.kind == QCS_QASM_TOKEN_ERROR)
      token.kind = qcs_qasm_char_token_kind(*p);
    if (token.kind == QCS_QASM_TOKEN_ERROR)
      qcs_qasm_token_set_error(&token, "unexpected character");
  }

  if (token.kind != QCS_QASM_TOKEN_ERROR && token.kind != QCS_QASM_TOKEN_STRING)
    token.length = (uint32_t)(end - p);
  qcs_qasm_lexer_move_over(lexer, p, end);
  lexer->cursor = end;
  return token;
}
