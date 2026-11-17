// Checks the gate matrices and control masks, no GPU needed

#undef NDEBUG
#include <assert.h>
#include <complex.h>
#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <string.h>

// Every gate is unitary: U† U = I
static void test_unitary(void) {
  QCS_Gate gates[] = {
      qcs_gate_create_id(0),    qcs_gate_create_x(0),
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
  QCS_Gate i = qcs_gate_create_id(0);
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

// Rotations, phase and U are unitary for any angle, on their target
static void test_rotations_unitary(void) {
  const double angles[] = {0, 0.3, QCS_PI / 2, QCS_PI, -1.2, 2 * QCS_PI, 7.5};
  for (size_t i = 0; i < sizeof angles / sizeof angles[0]; i++) {
    double a = angles[i];
    QCS_Gate gates[] = {
        qcs_gate_create_rx(3, a),         qcs_gate_create_ry(3, a),
        qcs_gate_create_rz(3, a),         qcs_gate_create_p(3, a),
        qcs_gate_create_u(3, a, 0.7, -2), qcs_gate_create_u(3, 1, a, a / 3),
    };
    for (size_t g = 0; g < sizeof gates / sizeof gates[0]; g++) {
      assert(qcs_gate_is_unitary(gates[g]));
      assert(gates[g].target == 3);
      assert(gates[g].control_ones == 0 && gates[g].control_zeros == 0);
    }
  }
}

// gate times e^{i·angle}
static QCS_Gate with_phase(QCS_Gate gate, double angle) {
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      gate.m[r][c] *= cexp(I * angle);
    }
  }
  return gate;
}

// U(theta, phi, lambda) of the OpenQASM 3 spec: H, RX, RY times e^{iθ/2},
// P exactly, iX at (π, 0, π), 2π-periodic in θ
static void test_u(void) {
  QCS_Gate h = qcs_gate_create_h(0);
  assert(qcs_gate_matrix_equal(qcs_gate_create_u(0, QCS_PI / 2, 0, QCS_PI),
                               with_phase(h, QCS_PI / 4)));
  assert(qcs_gate_matrix_equal(qcs_gate_create_u(0, QCS_PI, 0, QCS_PI),
                               with_phase(qcs_gate_create_x(0), QCS_PI / 2)));
  const double angles[] = {0, 0.3, QCS_PI / 2, QCS_PI, -1.2, 7.5};
  for (size_t i = 0; i < sizeof angles / sizeof angles[0]; i++) {
    double a = angles[i];
    assert(
        qcs_gate_matrix_equal(qcs_gate_create_u(0, a, -QCS_PI / 2, QCS_PI / 2),
                              with_phase(qcs_gate_create_rx(0, a), a / 2)));
    assert(qcs_gate_matrix_equal(qcs_gate_create_u(0, a, 0, 0),
                                 with_phase(qcs_gate_create_ry(0, a), a / 2)));
    assert(qcs_gate_matrix_equal(qcs_gate_create_u(0, 0, 0, a),
                                 qcs_gate_create_p(0, a)));
    assert(qcs_gate_matrix_equal(qcs_gate_create_u(0, a + 2 * QCS_PI, 0.7, -2),
                                 qcs_gate_create_u(0, a, 0.7, -2)));
  }
  // rotations by 0 are the identity, by pi the Pauli gate times -i
  QCS_Gate id = qcs_gate_create_id(0);
  assert(qcs_gate_matrix_equal(qcs_gate_create_rx(0, 0), id));
  assert(qcs_gate_matrix_equal(qcs_gate_create_ry(0, 0), id));
  assert(qcs_gate_matrix_equal(qcs_gate_create_rz(0, 0), id));
  QCS_Gate rx_pi = qcs_gate_create_rx(0, QCS_PI);
  QCS_Gate x = qcs_gate_create_x(0);
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      assert(qcs_c128_near(rx_pi.m[r][c], -I * x.m[r][c]));
    }
  }
  // P(pi) = Z, and RZ is not P: they differ by a global phase
  assert(qcs_gate_matrix_equal(qcs_gate_create_p(0, QCS_PI),
                               qcs_gate_create_z(0)));
  assert(!qcs_gate_matrix_equal(qcs_gate_create_rz(0, 1),
                                qcs_gate_create_p(0, 1)));
}

// Controlled gates carry the plain matrix plus control masks, on 1 and on 0
static void test_controls(void) {
  QCS_Gate cx = qcs_gate_create_cx(1, 0);
  assert(qcs_gate_matrix_equal(cx, qcs_gate_create_x(1)));
  assert(cx.target == 1);
  assert(cx.control_ones == 0b001 && cx.control_zeros == 0);

  // A second control makes it a Toffoli (CCX)
  qcs_gate_add_control_one(&cx, 2);
  assert(cx.control_ones == 0b101);

  qcs_gate_remove_control(&cx, 0);
  assert(cx.control_ones == 0b100);

  // a control on 0, removed like any control
  qcs_gate_add_control_zero(&cx, 3);
  assert(cx.control_ones == 0b0100 && cx.control_zeros == 0b1000);
  qcs_gate_remove_control(&cx, 3);
  assert(cx.control_zeros == 0);

  // adjoint and product keep both masks
  qcs_gate_add_control_zero(&cx, 3);
  QCS_Gate adjoint = qcs_gate_adjoint(cx);
  assert(adjoint.control_ones == 0b0100 && adjoint.control_zeros == 0b1000);
  QCS_Gate product = qcs_gate_mul(cx, cx);
  assert(product.control_ones == 0b0100 && product.control_zeros == 0b1000);
}

// Created gates carry their name and parameters, matrix operations clear
// them, controls keep them
static void test_labels(void) {
  QCS_Gate h = qcs_gate_create_h(0);
  assert(strcmp(h.name, "h") == 0 && h.param_count == 0);
  assert(strcmp(qcs_gate_create_cx(1, 0).name, "cx") == 0);
  assert(strcmp(qcs_gate_create_sx(0).name, "sx") == 0);
  QCS_Gate rz = qcs_gate_create_rz(0, 0.5);
  assert(strcmp(rz.name, "rz") == 0);
  assert(rz.param_count == 1 && rz.params[0] == 0.5);
  QCS_Gate u = qcs_gate_create_u(0, 1, 2, 3);
  assert(strcmp(u.name, "u") == 0 && u.param_count == 3);
  assert(u.params[0] == 1 && u.params[1] == 2 && u.params[2] == 3);

  assert(qcs_gate_mul(h, h).name == nullptr);
  assert(qcs_gate_mul(rz, rz).param_count == 0);
  assert(qcs_gate_adjoint(rz).name == nullptr);
  qcs_gate_add_control_one(&h, 1);
  assert(strcmp(h.name, "h") == 0);
}

// gate printed by qcs_gate_fdump is exactly expected
static void expect_print(QCS_Gate gate, const char *expected) {
  // print into a temporary file
  FILE *file = tmpfile();
  assert(file);
  qcs_gate_fdump(gate, file);

  // read it back
  char text[256];
  rewind(file);
  size_t length = fread(text, 1, sizeof text - 1, file);
  text[length] = '\0';
  fclose(file);
  if (strcmp(text, expected) != 0) {
    fprintf(stderr, "[e] printed \"%s\", expected \"%s\"\n", text, expected);
    assert(false);
  }
}

// One line per gate: name(params), target, every control lowest first; the
// matrix when unnamed
static void test_print(void) {
  // names and parameters
  expect_print(qcs_gate_create_h(0), "h q[0]");
  expect_print(qcs_gate_create_rz(2, 0.3), "rz(0.3) q[2]");
  expect_print(qcs_gate_create_p(0, QCS_PI / 2), "p(1.5708) q[0]");
  expect_print(qcs_gate_create_u(0, 1, 2, 3), "u(1, 2, 3) q[0]");

  // controls, the lowest one not qubit 0 included
  expect_print(qcs_gate_create_cx(1, 0), "cx q[1] ctrl q[0]");
  expect_print(qcs_gate_create_cx(0, 2), "cx q[0] ctrl q[2]");
  QCS_Gate h = qcs_gate_create_h(0);
  qcs_gate_add_control_one(&h, 3);
  qcs_gate_add_control_one(&h, 1);
  expect_print(h, "h q[0] ctrl q[1], q[3]");
  qcs_gate_add_control_zero(&h, 4);
  qcs_gate_add_control_zero(&h, 2);
  expect_print(h, "h q[0] ctrl q[1], q[3] negctrl q[2], q[4]");

  // unnamed: the matrix, signs on the imaginary parts
  QCS_Gate unnamed = {.m = {{0.5, 0}, {0, 0.25 - 0.75 * I}}, .target = 1};
  expect_print(unnamed, "unitary [[0.5000+0.0000i, 0.0000+0.0000i], "
                        "[0.0000+0.0000i, 0.2500-0.7500i]] q[1]");
}

// SX is a square root of X
static void test_sx(void) {
  QCS_Gate sx = qcs_gate_create_sx(0);
  assert(qcs_gate_is_unitary(sx));
  assert(qcs_gate_matrix_equal(qcs_gate_mul(sx, sx), qcs_gate_create_x(0)));
}

int main(void) {
  test_labels();
  test_print();
  test_sx();
  test_unitary();
  test_identities();
  test_rotations_unitary();
  test_u();
  test_controls();
  puts("gates ok");
  return 0;
}
