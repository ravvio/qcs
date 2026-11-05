// Checks building, validating, clearing and destroying circuits, no GPU needed

#undef NDEBUG
#include <assert.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// A circuit of qubit_count qubits, which must be valid
static QCS_Circuit new_circuit(uint32_t qubit_count) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(qubit_count, &circuit) == QCS_ERR_OK);
  return circuit;
}

// Push a gate that must be accepted
static void push_ok(QCS_Circuit *circuit, QCS_Gate gate) {
  assert(qcs_circuit_push_gate(circuit, gate) == QCS_ERR_OK);
}

// Push a gate that must be rejected with error, leaving the circuit as it was
static void push_fails(QCS_Circuit *circuit, QCS_Gate gate, QCS_Error error) {
  size_t count = qcs_circuit_gate_count(circuit);
  assert(qcs_circuit_push_gate(circuit, gate) == error);
  assert(qcs_circuit_gate_count(circuit) == count);
}

// A new circuit has its qubits and no gates
static void test_create(void) {
  QCS_Circuit circuit = new_circuit(3);
  assert(circuit.qubit_count == 3);
  assert(qcs_circuit_gate_count(&circuit) == 0);
  qcs_circuit_destroy(&circuit);
}

// Create sets every field, whatever was in the struct before
static void test_create_overwrites(void) {
  QCS_Circuit circuit;
  memset(&circuit, 0xAB, sizeof circuit);
  assert(qcs_circuit_create(2, &circuit) == QCS_ERR_OK);
  assert(circuit.gates == NULL);
  push_ok(&circuit, qcs_gate_create_h(0));
  qcs_circuit_destroy(&circuit);
}

// Up to QCS_MAX_QUBITS qubits, the control mask has one bit per qubit
static void test_create_max_qubits(void) {
  QCS_Circuit circuit = new_circuit(QCS_MAX_QUBITS);
  push_ok(&circuit, qcs_gate_create_cx(0, QCS_MAX_QUBITS - 1));
  qcs_circuit_destroy(&circuit);

  QCS_Circuit too_big;
  assert(qcs_circuit_create(QCS_MAX_QUBITS + 1, &too_big) ==
         QCS_ERR_MAX_QUBITS);
}

// Gates are kept in the order they were pushed: the Bell circuit
static void test_push_order(void) {
  QCS_Circuit bell = new_circuit(2);
  push_ok(&bell, qcs_gate_create_h(0));
  push_ok(&bell, qcs_gate_create_cx(1, 0));
  assert(qcs_circuit_gate_count(&bell) == 2);

  QCS_Gate first = bell.gates[0];
  assert(qcs_gate_matrix_equal(first, qcs_gate_create_h(0)));
  assert(first.target == 0 && first.controls == 0);

  QCS_Gate second = bell.gates[1];
  assert(qcs_gate_matrix_equal(second, qcs_gate_create_x(1)));
  assert(second.target == 1 && second.controls == 0b01);

  qcs_circuit_destroy(&bell);
}

// Gates on the last qubit fit: as target and as control
static void test_push_last_qubit(void) {
  QCS_Circuit circuit = new_circuit(3);
  push_ok(&circuit, qcs_gate_create_x(2));
  push_ok(&circuit, qcs_gate_create_cx(0, 2));

  QCS_Gate ccx = qcs_gate_create_cx(0, 1);
  qcs_gate_add_control(&ccx, 2);
  push_ok(&circuit, ccx);

  assert(qcs_circuit_gate_count(&circuit) == 3);
  qcs_circuit_destroy(&circuit);
}

// Invalid gates are rejected and not added
static void test_push_invalid(void) {
  QCS_Circuit circuit = new_circuit(3);
  push_ok(&circuit, qcs_gate_create_h(0));

  // Target past the last qubit
  push_fails(&circuit, qcs_gate_create_x(3), QCS_ERR_QUBIT_RANGE);
  push_fails(&circuit, qcs_gate_create_x(31), QCS_ERR_QUBIT_RANGE);

  // Control past the last qubit
  push_fails(&circuit, qcs_gate_create_cx(0, 3), QCS_ERR_QUBIT_RANGE);
  QCS_Gate ccx = qcs_gate_create_cx(0, 1);
  qcs_gate_add_control(&ccx, 5);
  push_fails(&circuit, ccx, QCS_ERR_QUBIT_RANGE);

  // A gate controlled by its own target
  push_fails(&circuit, qcs_gate_create_cx(1, 1), QCS_ERR_CONTROL_ON_TARGET);
  QCS_Gate x = qcs_gate_create_x(2);
  qcs_gate_add_control(&x, 2);
  push_fails(&circuit, x, QCS_ERR_CONTROL_ON_TARGET);

  // The valid gate is still the only one
  assert(qcs_circuit_gate_count(&circuit) == 1);
  assert(circuit.gates[0].target == 0);
  qcs_circuit_destroy(&circuit);
}

// Many pushes grow the array, earlier gates survive the reallocations
static void test_push_many(void) {
  QCS_Circuit circuit = new_circuit(8);
  for (uint32_t i = 0; i < 1000; i++) {
    push_ok(&circuit, qcs_gate_create_x(i % 8));
  }
  assert(qcs_circuit_gate_count(&circuit) == 1000);
  for (uint32_t i = 0; i < 1000; i++) {
    assert(circuit.gates[i].target == i % 8);
  }
  qcs_circuit_destroy(&circuit);
}

// Clear empties the circuit but keeps its memory for the next gates
static void test_clear(void) {
  QCS_Circuit circuit = new_circuit(2);
  push_ok(&circuit, qcs_gate_create_h(0));
  push_ok(&circuit, qcs_gate_create_h(1));
  const QCS_Gate *memory = circuit.gates;

  qcs_circuit_clear(&circuit);
  assert(qcs_circuit_gate_count(&circuit) == 0);
  assert(circuit.qubit_count == 2);

  push_ok(&circuit, qcs_gate_create_z(1));
  assert(qcs_circuit_gate_count(&circuit) == 1);
  assert(circuit.gates == memory);
  assert(circuit.gates[0].target == 1);

  qcs_circuit_destroy(&circuit);
}

// Clearing a circuit that never had gates is fine
static void test_clear_empty(void) {
  QCS_Circuit circuit = new_circuit(1);
  qcs_circuit_clear(&circuit);
  assert(qcs_circuit_gate_count(&circuit) == 0);
  qcs_circuit_destroy(&circuit);
}

// Destroy frees the gates and leaves an empty circuit, twice is harmless
static void test_destroy(void) {
  QCS_Circuit circuit = new_circuit(2);
  push_ok(&circuit, qcs_gate_create_h(0));

  qcs_circuit_destroy(&circuit);
  assert(circuit.gates == NULL);
  assert(qcs_circuit_gate_count(&circuit) == 0);

  qcs_circuit_destroy(&circuit);
}

// Two circuits don't share gates
static void test_independent(void) {
  QCS_Circuit a = new_circuit(1);
  QCS_Circuit b = new_circuit(1);
  push_ok(&a, qcs_gate_create_x(0));
  push_ok(&b, qcs_gate_create_z(0));
  push_ok(&b, qcs_gate_create_z(0));

  assert(qcs_circuit_gate_count(&a) == 1);
  assert(qcs_circuit_gate_count(&b) == 2);
  assert(qcs_gate_matrix_equal(a.gates[0], qcs_gate_create_x(0)));
  assert(qcs_gate_matrix_equal(b.gates[0], qcs_gate_create_z(0)));

  qcs_circuit_destroy(&a);
  qcs_circuit_destroy(&b);
}

int main(void) {
  test_create();
  test_create_overwrites();
  test_create_max_qubits();
  test_push_order();
  test_push_last_qubit();
  test_push_invalid();
  test_push_many();
  test_clear();
  test_clear_empty();
  test_destroy();
  test_independent();
  puts("circuit ok");
  return 0;
}
