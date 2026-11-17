#include "qcs/core.h"
#include <assert.h>
#include <math.h>
#include <qcs/gate.h>
#include <stdint.h>
#include <stdio.h>

// Identity gate, does nothing
QCS_Gate qcs_gate_create_id(uint32_t target) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, 1}},
      .target = target,
      .control_ones = 0,
      .name = "id",
  };
}

// Pauli-X gate, bit flip (classical not)
QCS_Gate qcs_gate_create_x(uint32_t target) {
  return (QCS_Gate){
      .m = {{0, 1}, {1, 0}},
      .target = target,
      .control_ones = 0,
      .name = "x",
  };
}

// Pauli-Y gate, bit and phase flip (Y = iXZ)
QCS_Gate qcs_gate_create_y(uint32_t target) {
  return (QCS_Gate){
      .m = {{0, -I}, {I, 0}},
      .target = target,
      .control_ones = 0,
      .name = "y",
  };
}

// Pauli-Z gate, phase flip
QCS_Gate qcs_gate_create_z(uint32_t target) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, -1}},
      .target = target,
      .control_ones = 0,
      .name = "z",
  };
}

// Controlled X gate
QCS_Gate qcs_gate_create_cx(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{0, 1}, {1, 0}},
      .target = target,
      .control_ones = 1u << control,
      .name = "cx",
  };
}

// Controlled Y gate
QCS_Gate qcs_gate_create_cy(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{0, -I}, {I, 0}},
      .target = target,
      .control_ones = 1u << control,
      .name = "cy",
  };
}

// Controlled Z gate
QCS_Gate qcs_gate_create_cz(uint32_t target, uint32_t control) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, -1}},
      .target = target,
      .control_ones = 1u << control,
      .name = "cz",
  };
}

// Hadamard gate, creates superposition
QCS_Gate qcs_gate_create_h(uint32_t target) {
  const double s = 1.0 / sqrt(2.0);
  return (QCS_Gate){
      .m = {{s, s}, {s, -s}},
      .target = target,
      .control_ones = 0,
      .name = "h",
  };
}

// Principal square root of X
QCS_Gate qcs_gate_create_sx(uint32_t target) {
  return (QCS_Gate){
      .m = {{(1 + I) / 2, (1 - I) / 2}, {(1 - I) / 2, (1 + I) / 2}},
      .target = target,
      .control_ones = 0,
      .name = "sx",
  };
}

// Rotation by theta around the X axis
QCS_Gate qcs_gate_create_rx(uint32_t target, double theta) {
  const double c = cos(theta / 2);
  const double s = sin(theta / 2);
  return (QCS_Gate){
      .m = {{c, -I * s}, {-I * s, c}},
      .target = target,
      .control_ones = 0,
      .name = "rx",
      .params = {theta},
      .param_count = 1,
  };
}

// Rotation by theta around the Y axis
QCS_Gate qcs_gate_create_ry(uint32_t target, double theta) {
  const double c = cos(theta / 2);
  const double s = sin(theta / 2);
  return (QCS_Gate){
      .m = {{c, -s}, {s, c}},
      .target = target,
      .control_ones = 0,
      .name = "ry",
      .params = {theta},
      .param_count = 1,
  };
}

// Rotation by theta around the Z axis
QCS_Gate qcs_gate_create_rz(uint32_t target, double theta) {
  return (QCS_Gate){
      .m = {{cexp(-I * theta / 2), 0}, {0, cexp(I * theta / 2)}},
      .target = target,
      .control_ones = 0,
      .name = "rz",
      .params = {theta},
      .param_count = 1,
  };
}

// Phase gate, phase lambda on |1⟩ (OpenQASM's u1)
QCS_Gate qcs_gate_create_p(uint32_t target, double lambda) {
  return (QCS_Gate){
      .m = {{1, 0}, {0, cexp(I * lambda)}},
      .target = target,
      .control_ones = 0,
      .name = "p",
      .params = {lambda},
      .param_count = 1,
  };
}

// General single-qubit gate U(theta, phi, lambda) of the OpenQASM 3 spec
QCS_Gate qcs_gate_create_u(uint32_t target, double theta, double phi,
                           double lambda) {
  const double complex plus = (1 + cexp(I * theta)) / 2;
  const double complex minus = (1 - cexp(I * theta)) / 2;
  return (QCS_Gate){
      .m = {{plus, -I * cexp(I * lambda) * minus},
            {I * cexp(I * phi) * minus, cexp(I * (phi + lambda)) * plus}},
      .target = target,
      .control_ones = 0,
      .name = "u",
      .params = {theta, phi, lambda},
      .param_count = 3,
  };
}

// Add a control qubit to a gate, acting when it is 1
void qcs_gate_add_control_one(QCS_Gate *gate, uint32_t control) {
  gate->control_ones |= 1u << control;
}

// Add a control qubit to a gate, acting when it is 0
void qcs_gate_add_control_zero(QCS_Gate *gate, uint32_t control) {
  gate->control_zeros |= 1u << control;
}

// Name and parameters of the label
void qcs_gate_set_label(QCS_Gate *gate, const char *name, const double *params,
                        uint8_t param_count) {
  assert(param_count <= QCS_GATE_MAX_PARAMS);
  gate->name = name;
  gate->param_count = param_count;
  for (uint8_t i = 0; i < param_count; i++) {
    gate->params[i] = params[i];
  }
}

// Remove a control qubit from a gate, on 1 or 0
void qcs_gate_remove_control(QCS_Gate *gate, uint32_t control) {
  gate->control_ones &= ~(1u << control);
  gate->control_zeros &= ~(1u << control);
}

// a · b, b is applied first
QCS_Gate qcs_gate_mul(QCS_Gate a, QCS_Gate b) {
  assert(a.target == b.target && a.control_ones == b.control_ones &&
         a.control_zeros == b.control_zeros);
  QCS_Gate result = {.target = a.target,
                     .control_ones = a.control_ones,
                     .control_zeros = a.control_zeros};
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      result.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c];
    }
  }
  return result;
}

// gate times e^{i·phase}
QCS_Gate qcs_gate_with_phase(QCS_Gate gate, double phase) {
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      gate.m[r][c] *= cexp(I * phase);
    }
  }
  return gate;
}

// Conjugate transpose
QCS_Gate qcs_gate_adjoint(QCS_Gate gate) {
  QCS_Gate result = {.target = gate.target,
                     .control_ones = gate.control_ones,
                     .control_zeros = gate.control_zeros};
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
    uint32_t used = gate.control_ones | gate.control_zeros | 1u << target;
    if (used >> control & 1) {
      continue;
    }
    // on 1 or on 0, at random
    if (qcs_random_next(random) & 1) {
      qcs_gate_add_control_one(&gate, control);
    } else {
      qcs_gate_add_control_zero(&gate, control);
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

// write the qubits of a mask, lowest first: before the first, ", " between
static void qcs_qubits_fdump(uint32_t qubits, const char *before, FILE *file) {
  for (uint32_t q = 0; q < QCS_MAX_QUBITS; q++) {
    if (qubits >> q & 1) {
      bool first = (qubits & ((1u << q) - 1)) == 0;
      fprintf(file, "%sq[%u]", first ? before : ", ", q);
    }
  }
}

// write formatted gate into file
void qcs_gate_fdump(QCS_Gate gate, FILE *file) {
  // name and parameters, or the matrix
  if (gate.name) {
    fputs(gate.name, file);
    if (gate.param_count > 0) {
      for (uint8_t p = 0; p < gate.param_count; p++) {
        fprintf(file, "%s%.6g", p == 0 ? "(" : ", ", gate.params[p]);
      }
      fputc(')', file);
    }
  } else {
    fputs("unitary [", file);
    for (int r = 0; r < 2; r++) {
      fputs(r == 0 ? "[" : ", [", file);
      for (int c = 0; c < 2; c++) {
        fprintf(file, "%s%.4f%+.4fi", c == 0 ? "" : ", ", creal(gate.m[r][c]),
                cimag(gate.m[r][c]));
      }
      fputc(']', file);
    }
    fputc(']', file);
  }

  // target
  fprintf(file, " q[%u]", gate.target);

  // controls on 1, then controls on 0
  qcs_qubits_fdump(gate.control_ones, " ctrl ", file);
  qcs_qubits_fdump(gate.control_zeros, " negctrl ", file);
}
