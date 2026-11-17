// OpenQASM syntax tree: arena, expression and statement nodes
#ifndef QCS_QASM_AST_DECL
#define QCS_QASM_AST_DECL

#include "qcs_qasm_lexer.h"
#include <qcs/qasm.h>
#include <stddef.h>
#include <stdint.h>

#define QCS_ARENA_BLOCK_SIZE (64 * 1024)

// Bump allocator, freed all at once
typedef struct {
  uint8_t **blocks; // stb_ds array
  size_t used;      // bytes used in the last block
  size_t capacity;  // size of the last block
} QCS_Arena;

// size zeroed bytes aligned for any type, NULL when out of memory
void *qcs_arena_alloc(QCS_Arena *arena, size_t size);
void qcs_arena_free(QCS_Arena *arena);

typedef enum : uint8_t {
  QCS_QASM_EXPR_INT,
  QCS_QASM_EXPR_FLOAT,
  QCS_QASM_EXPR_NAME,
  QCS_QASM_EXPR_UNARY,
  QCS_QASM_EXPR_BINARY,
  QCS_QASM_EXPR_CALL,
} QCS_QasmExprKind;

typedef enum : uint8_t {
  QCS_QASM_OP_ADD,
  QCS_QASM_OP_SUB,
  QCS_QASM_OP_MUL,
  QCS_QASM_OP_DIV,
  QCS_QASM_OP_MOD,
  QCS_QASM_OP_POW,
  QCS_QASM_OP_NEG,
} QCS_QasmOp;

typedef struct QCS_QasmExpr QCS_QasmExpr;
struct QCS_QasmExpr {
  QCS_QasmExprKind kind;
  // operator, name or literal position
  uint32_t line, column;
  // next in a list: gate parameters, set elements
  QCS_QasmExpr *next;
  union {
    int64_t integer;    // INT
    double number;      // FLOAT
    QCS_QasmToken name; // NAME
    struct {
      QCS_QasmOp op;
      QCS_QasmExpr *operand;
    } unary;
    struct {
      QCS_QasmOp op;
      QCS_QasmExpr *left, *right;
    } binary;
    struct {
      QCS_QasmToken name; // function, resolved when lowering
      QCS_QasmExpr *argument;
    } call;
  };
};

// One constructor per kind, positioned at `at`; NULL when out of memory
QCS_QasmExpr *qcs_qasm_expr_int(QCS_Arena *arena, QCS_QasmToken at,
                                int64_t value);
QCS_QasmExpr *qcs_qasm_expr_float(QCS_Arena *arena, QCS_QasmToken at,
                                  double value);
QCS_QasmExpr *qcs_qasm_expr_name(QCS_Arena *arena, QCS_QasmToken name);
QCS_QasmExpr *qcs_qasm_expr_unary(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmOp op, QCS_QasmExpr *operand);
QCS_QasmExpr *qcs_qasm_expr_binary(QCS_Arena *arena, QCS_QasmToken at,
                                   QCS_QasmOp op, QCS_QasmExpr *left,
                                   QCS_QasmExpr *right);
QCS_QasmExpr *qcs_qasm_expr_call(QCS_Arena *arena, QCS_QasmToken name,
                                 QCS_QasmExpr *argument);

// expr as an S-expression, `(+ 1 (* 2 x))`, into out (NUL-terminated,
// cut at size), returns the full length
size_t qcs_qasm_expr_dump(const QCS_QasmExpr *expr, char *out, size_t size);

// Type of a const, an input or a loop variable
typedef enum : uint8_t {
  QCS_QASM_TYPE_INT,
  QCS_QASM_TYPE_UINT,
  QCS_QASM_TYPE_FLOAT,
  QCS_QASM_TYPE_ANGLE,
} QCS_QasmType;

// Inclusive range start:end or start:step:end, step NULL for 1
typedef struct {
  QCS_QasmExpr *start, *step, *end;
} QCS_QasmRange;

// Identifier in a list: gate parameters and arguments
typedef struct QCS_QasmName QCS_QasmName;
struct QCS_QasmName {
  QCS_QasmToken token;
  QCS_QasmName *next;
};

typedef enum : uint8_t {
  QCS_QASM_INDEX_NONE,  // q
  QCS_QASM_INDEX_ONE,   // q[i]
  QCS_QASM_INDEX_RANGE, // q[a:b], q[a:s:b]
} QCS_QasmIndexKind;

// Qubit or bit operand
typedef struct QCS_QasmOperand QCS_QasmOperand;
struct QCS_QasmOperand {
  QCS_QasmIndexKind kind;
  QCS_QasmToken name;
  QCS_QasmExpr *index; // ONE
  QCS_QasmRange range; // RANGE
  QCS_QasmOperand *next;
};

typedef enum : uint8_t {
  QCS_QASM_MODIFIER_CTRL,
  QCS_QASM_MODIFIER_NEGCTRL,
  QCS_QASM_MODIFIER_INV,
  QCS_QASM_MODIFIER_POW,
} QCS_QasmModifierKind;

// Gate modifier before '@'
typedef struct QCS_QasmModifier QCS_QasmModifier;
struct QCS_QasmModifier {
  QCS_QasmModifierKind kind;
  uint32_t line, column;
  // ctrl and negctrl count, pow exponent; NULL when absent
  QCS_QasmExpr *argument;
  QCS_QasmModifier *next;
};

typedef enum : uint8_t {
  QCS_QASM_STMT_INCLUDE,
  QCS_QASM_STMT_DECLARE,
  QCS_QASM_STMT_CONST,
  QCS_QASM_STMT_INPUT,
  QCS_QASM_STMT_GATE_CALL,
  QCS_QASM_STMT_GATE_DEF,
  QCS_QASM_STMT_FOR_RANGE,
  QCS_QASM_STMT_FOR_SET,
  QCS_QASM_STMT_MEASURE,
  QCS_QASM_STMT_BARRIER,
  QCS_QASM_STMT_IF,
  QCS_QASM_STMT_RESET,
} QCS_QasmStmtKind;

typedef struct QCS_QasmStmt QCS_QasmStmt;
struct QCS_QasmStmt {
  QCS_QasmStmtKind kind;
  // first token
  uint32_t line, column;
  // next statement in the program or block
  QCS_QasmStmt *next;
  union {
    QCS_QasmToken include; // file name, without quotes
    struct {
      bool quantum; // qubit or qreg, else bit or creg
      QCS_QasmToken name;
      QCS_QasmExpr *size; // NULL: a single qubit or bit
    } declare;
    struct {
      QCS_QasmType type;
      QCS_QasmToken name;
      QCS_QasmExpr *value;
    } constant;
    struct {
      QCS_QasmType type;
      QCS_QasmToken name;
    } input;
    struct {
      QCS_QasmModifier *modifiers;
      QCS_QasmToken name;
      QCS_QasmExpr *params;
      QCS_QasmOperand *operands;
    } call;
    struct {
      QCS_QasmToken name;
      QCS_QasmName *params, *args;
      QCS_QasmStmt *body;
    } gate;
    struct {
      QCS_QasmType type;
      QCS_QasmToken variable;
      QCS_QasmRange range; // FOR_RANGE
      QCS_QasmExpr *set;   // FOR_SET
      QCS_QasmStmt *body;
    } loop;
    struct {
      QCS_QasmOperand *qubit, *bit;
    } measure;
    struct {
      QCS_QasmOperand *operands; // NULL: every qubit
    } barrier;
    struct {
      QCS_QasmOperand *bits; // c[i], c
      bool negate;           // !c[i]
      QCS_QasmExpr *value;   // bits == value; NULL: one bit, true when 1
      QCS_QasmStmt *body;
      QCS_QasmStmt *else_body; // NULL without else
    } branch;
    struct {
      QCS_QasmOperand *qubits; // q[i], q
    } reset;
  };
};

// Constructors, positioned at `at`; NULL when out of memory
QCS_QasmName *qcs_qasm_name(QCS_Arena *arena, QCS_QasmToken token);
QCS_QasmOperand *qcs_qasm_operand(QCS_Arena *arena, QCS_QasmToken name);
QCS_QasmOperand *qcs_qasm_operand_index(QCS_Arena *arena, QCS_QasmToken name,
                                        QCS_QasmExpr *index);
QCS_QasmOperand *qcs_qasm_operand_range(QCS_Arena *arena, QCS_QasmToken name,
                                        QCS_QasmRange range);
QCS_QasmModifier *qcs_qasm_modifier(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmModifierKind kind,
                                    QCS_QasmExpr *argument);
QCS_QasmStmt *qcs_qasm_stmt_include(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmToken file);
QCS_QasmStmt *qcs_qasm_stmt_declare(QCS_Arena *arena, QCS_QasmToken at,
                                    bool quantum, QCS_QasmToken name,
                                    QCS_QasmExpr *size);
QCS_QasmStmt *qcs_qasm_stmt_const(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmType type, QCS_QasmToken name,
                                  QCS_QasmExpr *value);
QCS_QasmStmt *qcs_qasm_stmt_input(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmType type, QCS_QasmToken name);
QCS_QasmStmt *qcs_qasm_stmt_gate_call(QCS_Arena *arena, QCS_QasmToken at,
                                      QCS_QasmModifier *modifiers,
                                      QCS_QasmToken name, QCS_QasmExpr *params,
                                      QCS_QasmOperand *operands);
QCS_QasmStmt *qcs_qasm_stmt_gate_def(QCS_Arena *arena, QCS_QasmToken at,
                                     QCS_QasmToken name, QCS_QasmName *params,
                                     QCS_QasmName *args, QCS_QasmStmt *body);
QCS_QasmStmt *qcs_qasm_stmt_for_range(QCS_Arena *arena, QCS_QasmToken at,
                                      QCS_QasmType type, QCS_QasmToken variable,
                                      QCS_QasmRange range, QCS_QasmStmt *body);
QCS_QasmStmt *qcs_qasm_stmt_for_set(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmType type, QCS_QasmToken variable,
                                    QCS_QasmExpr *set, QCS_QasmStmt *body);
QCS_QasmStmt *qcs_qasm_stmt_measure(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmOperand *qubit,
                                    QCS_QasmOperand *bit);
QCS_QasmStmt *qcs_qasm_stmt_barrier(QCS_Arena *arena, QCS_QasmToken at,
                                    QCS_QasmOperand *operands);
QCS_QasmStmt *qcs_qasm_stmt_if(QCS_Arena *arena, QCS_QasmToken at,
                               QCS_QasmOperand *bits, bool negate,
                               QCS_QasmExpr *value, QCS_QasmStmt *body,
                               QCS_QasmStmt *else_body);
QCS_QasmStmt *qcs_qasm_stmt_reset(QCS_Arena *arena, QCS_QasmToken at,
                                  QCS_QasmOperand *qubits);

// stmt as an S-expression, `(call h () (q[0]))`, bodies inside it; like
// qcs_qasm_expr_dump
size_t qcs_qasm_stmt_dump(const QCS_QasmStmt *stmt, char *out, size_t size);

#endif
