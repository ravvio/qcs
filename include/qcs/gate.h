#ifndef QCS_GATE_DECL
#define QCS_GATE_DECL

#include <complex.h>
#include <qcs/random.h>
#include <stdint.h>
#include <stdio.h>

#define QCS_GATE_MAX_PARAMS 4

typedef struct {
  double complex m[2][2];
  uint32_t target;
  // qubits that must be 1, qubits that must be 0
  uint32_t control_ones;
  uint32_t control_zeros;
  // what the viewer draws, ignored by the simulators; NULL when unnamed
  const char *name;
  double params[QCS_GATE_MAX_PARAMS];
  uint8_t param_count;
} QCS_Gate;

// Single qubit gates
QCS_Gate qcs_gate_create_id(uint32_t target);
QCS_Gate qcs_gate_create_x(uint32_t target);
QCS_Gate qcs_gate_create_y(uint32_t target);
QCS_Gate qcs_gate_create_z(uint32_t target);
QCS_Gate qcs_gate_create_h(uint32_t target);
// Principal square root of X
QCS_Gate qcs_gate_create_sx(uint32_t target);

// Controlled gates
QCS_Gate qcs_gate_create_cx(uint32_t target, uint32_t control);
QCS_Gate qcs_gate_create_cy(uint32_t target, uint32_t control);
QCS_Gate qcs_gate_create_cz(uint32_t target, uint32_t control);

// Rotations and phases, angles in radians; U as in the OpenQASM 3 spec
QCS_Gate qcs_gate_create_rx(uint32_t target, double theta);
QCS_Gate qcs_gate_create_ry(uint32_t target, double theta);
QCS_Gate qcs_gate_create_rz(uint32_t target, double theta);
QCS_Gate qcs_gate_create_p(uint32_t target, double lambda);
QCS_Gate qcs_gate_create_u(uint32_t target, double theta, double phi,
                           double lambda);

void qcs_gate_add_control_one(QCS_Gate *gate, uint32_t control);
void qcs_gate_add_control_zero(QCS_Gate *gate, uint32_t control);
void qcs_gate_remove_control(QCS_Gate *gate, uint32_t control);
// name and param_count parameters of the label
void qcs_gate_set_label(QCS_Gate *gate, const char *name, const double *params,
                        uint8_t param_count);

// A random X, Y, Z or H on a random qubit below qubit_count, with 0 to 2
// controls on 1 or 0, for tests and benchmarks
QCS_Gate qcs_gate_create_random(uint32_t qubit_count, QCS_Random *random);

// Matrix operations

// a · b, b applied first
QCS_Gate qcs_gate_mul(QCS_Gate a, QCS_Gate b);
// gate times e^{i·phase}
QCS_Gate qcs_gate_with_phase(QCS_Gate gate, double phase);
// Conjugate transpose, the inverse of a unitary gate
QCS_Gate qcs_gate_adjoint(QCS_Gate gate);
// U† U = I
bool qcs_gate_is_unitary(QCS_Gate gate);
// Same matrices within 1e-12, target and controls are ignored
bool qcs_gate_matrix_equal(QCS_Gate a, QCS_Gate b);

// Print

void qcs_gate_fdump(QCS_Gate gate, FILE *file);

#endif
