#include <qcs/circuit.h>
#include <qcs/core.h>
#include <qcs/state.h>
#include <stb_ds.h>
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
  uint64_t gate_mask = (UINT64_C(1) << gate.target);
  if ((gate.controls & gate_mask) != 0) {
    fprintf(stderr, "[e] invalid gate: gate controls include target %u\n",
            gate.target);
    return QCS_ERR_CONTROL_ON_TARGET;
  }
  uint64_t circuit_mask = (UINT64_C(1) << circuit->qubit_count) - 1;
  if ((gate.controls & ~circuit_mask) != 0) {
    fprintf(stderr,
            "[e] invalid circuit: gate controls %#x include qubits over the "
            "circuit qubit count %u\n",
            gate.controls, circuit->qubit_count);
    return QCS_ERR_QUBIT_RANGE;
  }

  arrput(circuit->gates, gate);
  return QCS_ERR_OK;
}

size_t qcs_circuit_gate_count(const QCS_Circuit *circuit) {
  return arrlenu(circuit->gates);
}

void qcs_circuit_clear(QCS_Circuit *circuit) { arrsetlen(circuit->gates, 0); }

void qcs_circuit_destroy(QCS_Circuit *circuit) { arrfree(circuit->gates); }

QCS_Error qcs_circuit_run(const QCS_Circuit *circuit, QCS_State *state) {
  if (state->n != circuit->qubit_count) {
    fprintf(stderr,
            "[e] invalid state: state has %u qubits, circuit is built for %u\n",
            state->n, circuit->qubit_count);
    return QCS_ERR_INVALID_STATE;
  }
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    qcs_state_apply_gate(state, circuit->gates[g]);
  }
  return QCS_ERR_OK;
}
