// Checks the bitstrings of patterns, no GPU needed

#undef NDEBUG
#include <assert.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// The bitstring of index on n qubits is expected
static void assert_bitstring(uint32_t n, size_t index, const char *expected) {
  char text[QCS_MAX_QUBITS + 1];
  assert(qcs_bitstring(n, index, text) == QCS_ERR_OK);
  assert(strcmp(text, expected) == 0);
}

// Qubit n-1 first, qubit 0 on the right
static void test_order(void) {
  assert_bitstring(3, 1, "001");
  assert_bitstring(3, 2, "010");
  assert_bitstring(3, 4, "100");
  assert_bitstring(3, 6, "110");
}

// Smallest and largest patterns
static void test_edges(void) {
  assert_bitstring(0, 0, "");
  assert_bitstring(1, 0, "0");
  assert_bitstring(1, 1, "1");
  assert_bitstring(3, 0, "000");
  assert_bitstring(3, 7, "111");
  assert_bitstring(QCS_MAX_QUBITS, 1, "00000000000000000000000000000001");
  assert_bitstring(QCS_MAX_QUBITS, ((size_t)1 << QCS_MAX_QUBITS) - 1,
                   "11111111111111111111111111111111");
}

// Indices past the qubits and too many qubits are rejected
static void test_invalid(void) {
  char text[QCS_MAX_QUBITS + 2];
  assert(qcs_bitstring(3, 8, text) == QCS_ERR_QUBIT_RANGE);
  assert(qcs_bitstring(0, 1, text) == QCS_ERR_QUBIT_RANGE);
  assert(qcs_bitstring(QCS_MAX_QUBITS + 1, 0, text) == QCS_ERR_MAX_QUBITS);
}

// X on qubit 1 of 3, measured: every shot reads 010
static void test_sampled_state(void) {
  QCS_State state;
  assert(qcs_state_create(3, &state) == QCS_ERR_OK);
  qcs_state_apply_gate(&state, qcs_gate_create_x(1));

  size_t outcomes[10];
  QCS_Random random = qcs_random_create(1);
  assert(qcs_state_sample(&state, 10, &random, outcomes) == QCS_ERR_OK);
  for (size_t s = 0; s < 10; s++) {
    assert_bitstring(3, outcomes[s], "010");
  }
  qcs_state_destroy(&state);
}

int main(void) {
  test_order();
  test_edges();
  test_invalid();
  test_sampled_state();
  puts("core ok");
  return 0;
}
