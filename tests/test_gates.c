// Checks the gate matrices and control masks, no GPU needed

#undef NDEBUG
#include <assert.h>
#include <qcs/qcs.h>
#include <stdio.h>

// Every gate is unitary: U† U = I
static void test_unitary(void) {
  QCS_Gate gates[] = {
      qcs_gate_create_i(0),     qcs_gate_create_x(0),
      qcs_gate_create_y(0),     qcs_gate_create_z(0),
      qcs_gate_create_h(0),     qcs_gate_create_cx(1, 0),
      qcs_gate_create_cy(1, 0), qcs_gate_create_cz(1, 0),
  };
  for (size_t i = 0; i < sizeof gates / sizeof gates[0]; i++) {
    assert(qcs_gate_is_unitary(gates[i]));
  }
}

// Textbook identities between gates on the same qubit
static void test_identities(void) {
  QCS_Gate i = qcs_gate_create_i(0);
  QCS_Gate x = qcs_gate_create_x(0);
  QCS_Gate y = qcs_gate_create_y(0);
  QCS_Gate z = qcs_gate_create_z(0);
  QCS_Gate h = qcs_gate_create_h(0);

  // Applying H, X, Y or Z twice does nothing
  assert(qcs_gate_matrix_equal(qcs_gate_mul(h, h), i));
  assert(qcs_gate_matrix_equal(qcs_gate_mul(x, x), i));
  assert(qcs_gate_matrix_equal(qcs_gate_mul(y, y), i));
  assert(qcs_gate_matrix_equal(qcs_gate_mul(z, z), i));

  // H swaps the X and Z bases: H Z H = X, H X H = Z
  assert(qcs_gate_matrix_equal(qcs_gate_mul(h, qcs_gate_mul(z, h)), x));
  assert(qcs_gate_matrix_equal(qcs_gate_mul(h, qcs_gate_mul(x, h)), z));

  // X, Y, Z, H are their own inverse
  assert(qcs_gate_matrix_equal(qcs_gate_adjoint(y), y));
  assert(qcs_gate_matrix_equal(qcs_gate_adjoint(h), h));
}

// Controlled gates carry the plain matrix plus a control mask
static void test_controls(void) {
  QCS_Gate cx = qcs_gate_create_cx(1, 0);
  assert(qcs_gate_matrix_equal(cx, qcs_gate_create_x(1)));
  assert(cx.target == 1);
  assert(cx.controls == 0b001);

  // A second control makes it a Toffoli (CCX)
  qcs_gate_add_control(&cx, 2);
  assert(cx.controls == 0b101);

  qcs_gate_remove_control(&cx, 0);
  assert(cx.controls == 0b100);
}

int main(void) {
  test_unitary();
  test_identities();
  test_controls();
  puts("gates ok");
  return 0;
}
