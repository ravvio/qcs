#include <assert.h>
#include <math.h>
#include <qcs/gate.h>
#include <stdint.h>

// Identity gate, does nothing
QCS_Gate qcs_gate_create_id(uint32_t target) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, 1}},
      .target = target,
      .controls = 0,
  };
}

// Pauli-X gate, bit flip (classical not)
QCS_Gate qcs_gate_create_x(uint32_t target) {
  return (QCS_Gate){
      .m = {{0, 1}, {1, 0}},
      .target = target,
      .controls = 0,
  };
}

// Pauli-Y gate, bit and phase flip (Y = iXZ)
QCS_Gate qcs_gate_create_y(uint32_t target) {
  return (QCS_Gate){
      .m = {{0, -I}, {I, 0}},
      .target = target,
      .controls = 0,
  };
}

// Pauli-Z gate, phase flip
QCS_Gate qcs_gate_create_z(uint32_t target) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, -1}},
      .target = target,
  };
}

// Controlled X gate
QCS_Gate qcs_gate_create_cx(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{0, 1}, {1, 0}},
      .target = target,
      .controls = 1u << control,
  };
}

// Controlled Y gate
QCS_Gate qcs_gate_create_cy(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{0, -I}, {I, 0}},
      .target = target,
      .controls = 1u << control,
  };
}

// Controlled Z gate
QCS_Gate qcs_gate_create_cz(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, -1}},
      .target = target,
      .controls = 1u << control,
  };
}

// Hadamard gate, creates superposition
QCS_Gate qcs_gate_create_h(uint32_t target) {
  const double s = 1.0 / sqrt(2.0);
  return (QCS_Gate){
      .m = {{s, s}, {s, -s}},
      .target = target,
      .controls = 0,
  };
}

// Add a control qubit to a gate
void qcs_gate_add_control(QCS_Gate *gate, uint32_t control) {
  gate->controls |= 1u << control;
}

// Remove a control qubit from a gate
void qcs_gate_remove_control(QCS_Gate *gate, uint32_t control) {
  gate->controls &= ~(1u << control);
}

// a · b, b is applied first
QCS_Gate qcs_gate_mul(QCS_Gate a, QCS_Gate b) {
  assert(a.target == b.target && a.controls == b.controls);
  QCS_Gate result = a;
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      result.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c];
    }
  }
  return result;
}

// Conjugate transpose
QCS_Gate qcs_gate_adjoint(QCS_Gate gate) {
  QCS_Gate result = gate;
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      result.m[r][c] = conj(gate.m[c][r]);
    }
  }
  return result;
}

QCS_Gate qcs_gate_create_random(uint32_t qubit_count, QCS_Random *random) {
  uint32_t target = qcs_random_next(random) % qubit_count;
  QCS_Gate gate;
  switch (qcs_random_next(random) % 4) {
  case 0:
    gate = qcs_gate_create_x(target);
    break;
  case 1:
    gate = qcs_gate_create_y(target);
    break;
  case 2:
    gate = qcs_gate_create_z(target);
    break;
  default:
    gate = qcs_gate_create_h(target);
    break;
  }
  uint32_t controls = qcs_random_next(random) % 3;
  for (uint32_t c = 0; c < controls; c++) {
    uint32_t control = qcs_random_next(random) % qubit_count;
    if (control != target) {
      qcs_gate_add_control(&gate, control);
    }
  }
  return gate;
}

// U† U = I
bool qcs_gate_is_unitary(QCS_Gate gate) {
  QCS_Gate product = qcs_gate_mul(qcs_gate_adjoint(gate), gate);
  return qcs_gate_matrix_equal(product, qcs_gate_create_id(gate.target));
}

// Same matrices within 1e-12
bool qcs_gate_matrix_equal(QCS_Gate a, QCS_Gate b) {
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      if (cabs(a.m[r][c] - b.m[r][c]) > 1e-12) {
        return false;
      }
    }
  }
  return true;
}
