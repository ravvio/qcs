#include "qcs/gate.h"
#include <qcs/circuit.h>
#include <qcs/core.h>
#include <qcs/state.h>
#include <stb_ds.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

QCS_Error qcs_circuit_create(uint32_t qubit_count, QCS_Circuit *out) {
  if (qubit_count > QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] invalid circuit: qubit count %u over maximum %u\n",
            qubit_count, QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }
  *out = (QCS_Circuit){.qubit_count = qubit_count};
  return QCS_ERR_OK;
}

QCS_Error qcs_circuit_push_gate(QCS_Circuit *circuit, QCS_Gate gate) {
  if (gate.target >= circuit->qubit_count) {
    fprintf(stderr,
            "[e] invalid circuit: gate target %u, circuit has %u qubits\n",
            gate.target, circuit->qubit_count);
    return QCS_ERR_QUBIT_RANGE;
  }
  uint32_t controls = gate.control_ones | gate.control_zeros;
  uint64_t gate_mask = (UINT64_C(1) << gate.target);
  if ((controls & gate_mask) != 0) {
    fprintf(stderr, "[e] invalid gate: gate controls include target %u\n",
            gate.target);
    return QCS_ERR_CONTROL_ON_TARGET;
  }
  if ((gate.control_ones & gate.control_zeros) != 0) {
    fprintf(stderr, "[e] invalid gate: qubits %#x are controls on 1 and on 0\n",
            gate.control_ones & gate.control_zeros);
    return QCS_ERR_CONTROL_ONE_AND_ZERO;
  }
  uint64_t circuit_mask = (UINT64_C(1) << circuit->qubit_count) - 1;
  if ((controls & ~circuit_mask) != 0) {
    fprintf(stderr,
            "[e] invalid circuit: gate controls %#x include qubits over the "
            "circuit qubit count %u\n",
            controls, circuit->qubit_count);
    return QCS_ERR_QUBIT_RANGE;
  }

  arrput(circuit->gates, gate);
  return QCS_ERR_OK;
}

size_t qcs_circuit_gate_count(const QCS_Circuit *circuit) {
  return arrlenu(circuit->gates);
}

void qcs_circuit_clear(QCS_Circuit *circuit) { arrsetlen(circuit->gates, 0); }

// free the gates, leave circuit empty
void qcs_circuit_destroy(QCS_Circuit *circuit) {
  arrfree(circuit->gates);
  *circuit = (QCS_Circuit){0};
}

// write circuit, its global phase then one gate per line
void qcs_circuit_fdump(const QCS_Circuit *circuit, FILE *file) {
  if (circuit->global_phase != 0) {
    fprintf(file, "gphase(%.6g)\n", circuit->global_phase);
  }
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    qcs_gate_fdump(circuit->gates[g], file);
    fputc('\n', file);
  }
}

QCS_Error qcs_circuit_run(const QCS_Circuit *circuit, QCS_State *state) {
  if (circuit->qubit_count > state->n) {
    fprintf(stderr,
            "[e] invalid state: state has %u qubits, circuit is built for %u\n",
            state->n, circuit->qubit_count);
    return QCS_ERR_INVALID_STATE;
  }
  // the gates, the global phase on the first
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    QCS_Gate gate = circuit->gates[g];
    if (g == 0) {
      gate = qcs_gate_with_phase(gate, circuit->global_phase);
    }
    qcs_state_apply_gate(state, gate);
  }
  return QCS_ERR_OK;
}
