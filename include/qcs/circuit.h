#ifndef QCS_CIRCUIT_DECL
#define QCS_CIRCUIT_DECL

#include <qcs/core.h>
#include <qcs/gate.h>
#include <qcs/state.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint32_t qubit_count;
  QCS_Gate *gates;
  // e^{i·global_phase} on the whole state
  double global_phase;
} QCS_Circuit;

QCS_Error qcs_circuit_create(uint32_t qubit_count, QCS_Circuit *out);
QCS_Error qcs_circuit_push_gate(QCS_Circuit *circuit, QCS_Gate gate);
size_t qcs_circuit_gate_count(const QCS_Circuit *circuit);
void qcs_circuit_clear(QCS_Circuit *circuit);
void qcs_circuit_destroy(QCS_Circuit *circuit);
// Apply the gates in order to state
QCS_Error qcs_circuit_run(const QCS_Circuit *circuit, QCS_State *state);

// Print

void qcs_circuit_fdump(const QCS_Circuit *circuit, FILE *file);

#endif
