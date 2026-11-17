// Checks the OpenQASM 2 lexer: token kinds, text, values, positions, errors

#undef NDEBUG
#include "qasm/qcs_qasm_lexer.h"
#include <assert.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// Token text equals text
static bool text_is(QCS_QasmToken token, const char *text) {
  return token.length == strlen(text) &&
         memcmp(token.start, text, token.length) == 0;
}

// Next token, which must be of kind at line:column
static QCS_QasmToken expect(QCS_QasmLexer *lexer, QCS_QasmTokenKind kind,
                            uint32_t line, uint32_t column) {
  QCS_QasmToken token = qcs_qasm_lexer_next(lexer);
  assert(token.kind == kind);
  assert(token.line == line);
  assert(token.column == column);
  return token;
}

// Lexing source gives kinds, up to and including EOF or ERROR
static void expect_kinds(const char *source, const QCS_QasmTokenKind *kinds) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, source);
  for (size_t i = 0;; i++) {
    QCS_QasmToken token = qcs_qasm_lexer_next(&lexer);
    assert(token.kind == kinds[i]);
    if (kinds[i] == QCS_QASM_TOKEN_EOF || kinds[i] == QCS_QASM_TOKEN_ERROR)
      return;
  }
}

// The only token of source, a number of kind and value
static void expect_number(const char *source, QCS_QasmTokenKind kind,
                          double value) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, source);
  QCS_QasmToken token = expect(&lexer, kind, 1, 1);
  assert(text_is(token, source));
  if (kind == QCS_QASM_TOKEN_INT) {
    assert(token.integer == (uint64_t)value);
  } else {
    assert(qcs_f64_near(token.number, value));
  }
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, (uint32_t)strlen(source) + 1);
}

// Integers past 2^53 stay exact, up to 2^64 - 1
static void test_large_integers(void) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "9223372036854775807 18446744073709551615");
  assert(expect(&lexer, QCS_QASM_TOKEN_INT, 1, 1).integer == INT64_MAX);
  assert(expect(&lexer, QCS_QASM_TOKEN_INT, 1, 21).integer == UINT64_MAX);
  qcs_qasm_lexer_init(&lexer, "9007199254740993");
  assert(expect(&lexer, QCS_QASM_TOKEN_INT, 1, 1).integer == 9007199254740993u);
  qcs_qasm_lexer_init(&lexer, "18446744073709551616");
  QCS_QasmToken token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);
  assert(text_is(token, "number too large"));
}

// Header line: kinds, text, value and 1-based positions
static void test_header(void) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "OPENQASM 2.0;");
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1), "OPENQASM"));
  QCS_QasmToken version = expect(&lexer, QCS_QASM_TOKEN_REAL, 1, 10);
  assert(text_is(version, "2.0"));
  assert(qcs_f64_near(version.number, 2.0));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_SEMICOLON, 1, 13), ";"));
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 14);
}

// Measurement with an arrow
static void test_measure(void) {
  expect_kinds("measure q[0] -> c[0];", (QCS_QasmTokenKind[]){
                                            QCS_QASM_TOKEN_IDENT,
                                            QCS_QASM_TOKEN_IDENT,
                                            QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                                            QCS_QASM_TOKEN_INT,
                                            QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                                            QCS_QASM_TOKEN_ARROW,
                                            QCS_QASM_TOKEN_IDENT,
                                            QCS_QASM_TOKEN_SQUARE_BRACKET_OPEN,
                                            QCS_QASM_TOKEN_INT,
                                            QCS_QASM_TOKEN_SQUARE_BRACKET_CLOSE,
                                            QCS_QASM_TOKEN_SEMICOLON,
                                            QCS_QASM_TOKEN_EOF,
                                        });
}

// Integers have neither a point nor an exponent, reals have one or both
static void test_numbers(void) {
  expect_number("0", QCS_QASM_TOKEN_INT, 0);
  expect_number("42", QCS_QASM_TOKEN_INT, 42);
  expect_number("1.5", QCS_QASM_TOKEN_REAL, 1.5);
  expect_number(".5", QCS_QASM_TOKEN_REAL, 0.5);
  expect_number("5.", QCS_QASM_TOKEN_REAL, 5);
  expect_number("2e-3", QCS_QASM_TOKEN_REAL, 0.002);
  expect_number("1.5E+2", QCS_QASM_TOKEN_REAL, 150);
  expect_number("3e2", QCS_QASM_TOKEN_REAL, 300);
}

// Hex, octal and binary integers, '_' between digits
static void test_number_bases(void) {
  expect_number("0x10", QCS_QASM_TOKEN_INT, 16);
  expect_number("0x1F", QCS_QASM_TOKEN_INT, 31);
  expect_number("0XfF", QCS_QASM_TOKEN_INT, 255);
  expect_number("0o17", QCS_QASM_TOKEN_INT, 15);
  expect_number("0b101", QCS_QASM_TOKEN_INT, 5);
  expect_number("0B11", QCS_QASM_TOKEN_INT, 3);
  expect_number("1_000", QCS_QASM_TOKEN_INT, 1000);
  expect_number("0xFF_FF", QCS_QASM_TOKEN_INT, 65535);
  expect_number("1_000.2_5", QCS_QASM_TOKEN_REAL, 1000.25);
  expect_number("1e1_0", QCS_QASM_TOKEN_REAL, 1e10);
}

// Only the number's own characters are converted
static void test_number_edges(void) {
  // a prefix without digits of its base
  expect_kinds("0x",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_IDENT,
                                     QCS_QASM_TOKEN_EOF});
  expect_kinds("0b2",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_IDENT,
                                     QCS_QASM_TOKEN_EOF});
  // '_' not between two digits
  expect_kinds("1__0",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_IDENT,
                                     QCS_QASM_TOKEN_EOF});
  expect_kinds("1_",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_IDENT,
                                     QCS_QASM_TOKEN_EOF});
  // exponent without digits
  expect_kinds("1e",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_IDENT,
                                     QCS_QASM_TOKEN_EOF});
  // lone point
  expect_kinds(".", (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_ERROR});
}

// A sign is never part of a number
static void test_minus(void) {
  expect_kinds("pi-1",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_IDENT, QCS_QASM_TOKEN_MINUS,
                                     QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_EOF});
  expect_kinds(
      "2*-3", (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_INT, QCS_QASM_TOKEN_ASTERISK,
                                    QCS_QASM_TOKEN_MINUS, QCS_QASM_TOKEN_INT,
                                    QCS_QASM_TOKEN_EOF});
  expect_kinds("x->y",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_IDENT, QCS_QASM_TOKEN_ARROW,
                                     QCS_QASM_TOKEN_IDENT, QCS_QASM_TOKEN_EOF});
}

// Identifiers start with a letter or underscore, then digits too
static void test_identifiers(void) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "_a1 U CX\tcu3");
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1), "_a1"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 5), "U"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 7), "CX"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 10), "cu3"));
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 13);

  // π, τ, ℇ, one column each
  qcs_qasm_lexer_init(&lexer, "\xcf\x80 \xcf\x84 \xe2\x84\x87 \xcf\x80x");
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1), "\xcf\x80"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 3), "\xcf\x84"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 5), "\xe2\x84\x87"));
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 7), "\xcf\x80x"));
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 9);
}

// Every two-char symbol, longest match first
static void test_double_symbols(void) {
  expect_kinds("-> == ** != <= >= && || << >>",
               (QCS_QasmTokenKind[]){
                   QCS_QASM_TOKEN_ARROW,
                   QCS_QASM_TOKEN_EQ_EQ,
                   QCS_QASM_TOKEN_ASTERISK_ASTERISK,
                   QCS_QASM_TOKEN_NOT_EQ,
                   QCS_QASM_TOKEN_LESSER_EQ,
                   QCS_QASM_TOKEN_GREATER_EQ,
                   QCS_QASM_TOKEN_AMP_AMP,
                   QCS_QASM_TOKEN_PIPE_PIPE,
                   QCS_QASM_TOKEN_LESSER_LESSER,
                   QCS_QASM_TOKEN_GREATER_GREATER,
                   QCS_QASM_TOKEN_EOF,
               });
  // two then one, and a sign after a power
  expect_kinds("<<=a**-b", (QCS_QasmTokenKind[]){
                               QCS_QASM_TOKEN_LESSER_LESSER,
                               QCS_QASM_TOKEN_EQ,
                               QCS_QASM_TOKEN_IDENT,
                               QCS_QASM_TOKEN_ASTERISK_ASTERISK,
                               QCS_QASM_TOKEN_MINUS,
                               QCS_QASM_TOKEN_IDENT,
                               QCS_QASM_TOKEN_EOF,
                           });
}

// Every single-char symbol
static void test_symbols(void) {
  expect_kinds("=<>!~&|%:@", (QCS_QasmTokenKind[]){
                                 QCS_QASM_TOKEN_EQ,
                                 QCS_QASM_TOKEN_LESSER,
                                 QCS_QASM_TOKEN_GREATER,
                                 QCS_QASM_TOKEN_NOT,
                                 QCS_QASM_TOKEN_TILDE,
                                 QCS_QASM_TOKEN_AMP,
                                 QCS_QASM_TOKEN_PIPE,
                                 QCS_QASM_TOKEN_PERCENT,
                                 QCS_QASM_TOKEN_COLON,
                                 QCS_QASM_TOKEN_AT,
                                 QCS_QASM_TOKEN_EOF,
                             });
  expect_kinds(";,()[]{}+-*/^", (QCS_QasmTokenKind[]){
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
                                    QCS_QASM_TOKEN_EOF,
                                });
}

// Comments run to the end of the line, a lone slash divides
static void test_comments(void) {
  const char *sources[] = {"a/b // c\nd", "a/b // c\r\nd"};
  for (size_t i = 0; i < 2; i++) {
    QCS_QasmLexer lexer;
    qcs_qasm_lexer_init(&lexer, sources[i]);
    expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
    expect(&lexer, QCS_QASM_TOKEN_SLASH, 1, 2);
    expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 3);
    assert(text_is(expect(&lexer, QCS_QASM_TOKEN_IDENT, 2, 1), "d"));
    expect(&lexer, QCS_QASM_TOKEN_EOF, 2, 2);
  }

  // comment at the end of the file
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "x // end");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 3);

  // block comments, on one line and across lines
  qcs_qasm_lexer_init(&lexer, "a/* x */b");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 9);
  qcs_qasm_lexer_init(&lexer, "a /* x\n yy */ b");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 2, 8);
  qcs_qasm_lexer_init(&lexer, "/**/x/* // */y");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 5);
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 14);

  // a closing mark alone is two symbols
  expect_kinds("*/",
               (QCS_QasmTokenKind[]){QCS_QASM_TOKEN_ASTERISK,
                                     QCS_QASM_TOKEN_SLASH, QCS_QASM_TOKEN_EOF});
}

// String text is between the quotes
static void test_string(void) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "include \"qelib1.inc\";");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_STRING, 1, 9), "qelib1.inc"));
  expect(&lexer, QCS_QASM_TOKEN_SEMICOLON, 1, 21);
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 22);

  // empty string
  qcs_qasm_lexer_init(&lexer, "\"\"");
  assert(text_is(expect(&lexer, QCS_QASM_TOKEN_STRING, 1, 1), ""));
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 3);
}

// Condition with ==
static void test_eq_eq(void) {
  expect_kinds("if(c==1)", (QCS_QasmTokenKind[]){
                               QCS_QASM_TOKEN_IDENT,
                               QCS_QASM_TOKEN_ROUND_BRACKET_OPEN,
                               QCS_QASM_TOKEN_IDENT,
                               QCS_QASM_TOKEN_EQ_EQ,
                               QCS_QASM_TOKEN_INT,
                               QCS_QASM_TOKEN_ROUND_BRACKET_CLOSE,
                               QCS_QASM_TOKEN_EOF,
                           });
}

// Errors carry a message and the position of the offending character
static void test_errors(void) {
  QCS_QasmLexer lexer;

  // unterminated string, at the opening quote
  qcs_qasm_lexer_init(&lexer, "\"abc");
  QCS_QasmToken token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);
  assert(text_is(token, "unterminated string"));
  qcs_qasm_lexer_init(&lexer, "x \"abc\ny\"");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 3);

  // character outside the language
  qcs_qasm_lexer_init(&lexer, "x ? 1");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 3);
  assert(text_is(token, "unexpected character"));

  // unterminated block comment, at its start, then EOF after it
  qcs_qasm_lexer_init(&lexer, "a /* b\nc");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 3);
  assert(text_is(token, "unterminated comment"));
  expect(&lexer, QCS_QASM_TOKEN_EOF, 2, 2);

  // integer too large for 64 bits
  qcs_qasm_lexer_init(&lexer, "0x1FFFFFFFFFFFFFFFF");
  token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);
  assert(text_is(token, "number too large"));

  // unknown character on line 3
  qcs_qasm_lexer_init(&lexer, "a;\n// b\n  #");
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_SEMICOLON, 1, 2);
  expect(&lexer, QCS_QASM_TOKEN_ERROR, 3, 3);

  // byte above 127
  qcs_qasm_lexer_init(&lexer, "\xc3\xa9");
  expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);

  // the lexer moves past an error
  qcs_qasm_lexer_init(&lexer, "$x");
  expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_IDENT, 1, 2);

  // number too long
  char source[80];
  memset(source, '1', sizeof source - 1);
  source[sizeof source - 1] = '\0';
  qcs_qasm_lexer_init(&lexer, source);
  token = expect(&lexer, QCS_QASM_TOKEN_ERROR, 1, 1);
  assert(text_is(token, "number too long"));
}

// EOF at the end, again on every later call
static void test_eof(void) {
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, "");
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 1);
  expect(&lexer, QCS_QASM_TOKEN_EOF, 1, 1);

  qcs_qasm_lexer_init(&lexer, "  \n ");
  expect(&lexer, QCS_QASM_TOKEN_EOF, 2, 2);
  expect(&lexer, QCS_QASM_TOKEN_EOF, 2, 2);
}

// A whole Bell circuit lexes without errors
static void test_bell(void) {
  const char *source = "OPENQASM 2.0;\n"
                       "include \"qelib1.inc\";\n"
                       "qreg q[2];\n"
                       "creg c[2];\n"
                       "h q[0];\n"
                       "cx q[0], q[1];\n"
                       "measure q -> c;\n";
  QCS_QasmLexer lexer;
  qcs_qasm_lexer_init(&lexer, source);
  size_t count = 0;
  QCS_QasmToken token;
  while ((token = qcs_qasm_lexer_next(&lexer)).kind != QCS_QASM_TOKEN_EOF) {
    assert(token.kind != QCS_QASM_TOKEN_ERROR);
    count++;
  }
  assert(count == 40);
  assert(token.line == 8 && token.column == 1);
}

int main(void) {
  test_header();
  test_measure();
  test_numbers();
  test_number_bases();
  test_large_integers();
  test_number_edges();
  test_minus();
  test_identifiers();
  test_double_symbols();
  test_symbols();
  test_comments();
  test_string();
  test_eq_eq();
  test_errors();
  test_eof();
  test_bell();
  puts("qasm lexer ok");
  return 0;
}
