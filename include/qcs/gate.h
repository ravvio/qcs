#ifndef QCS_GATE_DECL
#define QCS_GATE_DECL

#include <complex.h>
#include <qcs/random.h>
#include <stdint.h>

typedef struct {
  double complex m[2][2];
  uint32_t target;
  uint32_t controls;
} QCS_Gate;

// Single qubit gates
QCS_Gate qcs_gate_create_id(uint32_t target);
QCS_Gate qcs_gate_create_x(uint32_t target);
QCS_Gate qcs_gate_create_y(uint32_t target);
QCS_Gate qcs_gate_create_z(uint32_t target);
QCS_Gate qcs_gate_create_h(uint32_t target);

// Controlled gates
QCS_Gate qcs_gate_create_cx(uint32_t target, uint32_t control);
QCS_Gate qcs_gate_create_cy(uint32_t target, uint32_t control);
QCS_Gate qcs_gate_create_cz(uint32_t target, uint32_t control);

void qcs_gate_add_control(QCS_Gate *gate, uint32_t control);
void qcs_gate_remove_control(QCS_Gate *gate, uint32_t control);

// A random X, Y, Z or H on a random qubit below qubit_count, with 0 to 2
// controls, for tests and benchmarks
QCS_Gate qcs_gate_create_random(uint32_t qubit_count, QCS_Random *random);

// Matrix operations

// a · b, b applied first
QCS_Gate qcs_gate_mul(QCS_Gate a, QCS_Gate b);
// Conjugate transpose, the inverse of a unitary gate
QCS_Gate qcs_gate_adjoint(QCS_Gate gate);
// U† U = I
bool qcs_gate_is_unitary(QCS_Gate gate);
// Same matrices within 1e-12, target and controls are ignored
bool qcs_gate_matrix_equal(QCS_Gate a, QCS_Gate b);

#endif
