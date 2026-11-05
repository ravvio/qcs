// Checks the CPU state vector: creation, gates on every target, controls,
// circuits and the invariants unitary gates keep. No GPU needed.

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>

static const double S = 0.70710678118654752440; // 1/√2

// *** ASSERTIONS ***

// A state of n qubits, which must be created
static QCS_State new_state(uint32_t n) {
  QCS_State state;
  assert(qcs_state_create(n, &state) == QCS_ERR_OK);
  return state;
}

// The basis state |index⟩, which must be created
static QCS_State basis_state(uint32_t n, size_t index) {
  QCS_State state;
  assert(qcs_state_create_basis(n, index, &state) == QCS_ERR_OK);
  return state;
}

// Every amplitude equals expected (2^n values)
static void assert_amps(const QCS_State *state,
                        const double complex *expected) {
  for (size_t i = 0; i < qcs_state_size(state); i++) {
    assert(qcs_ceq(state->amps[i], expected[i]));
  }
}

// Only amplitude index is non-zero, and it is 1
static void assert_basis(const QCS_State *state, size_t index) {
  for (size_t i = 0; i < qcs_state_size(state); i++) {
    assert(qcs_ceq(state->amps[i], i == index ? 1 : 0));
  }
}

// *** CREATE / DESTROY ***

// A new state is |0…0⟩: amplitude 1 at index 0
static void test_create(void) {
  uint32_t sizes[] = {0, 1, 3, 10};
  for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
    QCS_State state = new_state(sizes[k]);
    assert(state.n == sizes[k]);
    assert_basis(&state, 0);
    qcs_state_destroy(&state);
  }
}

static void test_create_max_qubits(void) {
  QCS_State state;
  assert(qcs_state_create(QCS_MAX_QUBITS + 1, &state) == QCS_ERR_MAX_QUBITS);
}

// A random state has total probability 1
static void test_create_random(void) {
  QCS_Random random = qcs_random_create(17);
  for (uint32_t n = 0; n <= 6; n++) {
    QCS_State state;
    assert(qcs_state_create_random(n, &random, &state) == QCS_ERR_OK);
    assert(state.n == n);
    assert(qcs_eq(qcs_state_norm(&state), 1));
    qcs_state_destroy(&state);
  }
}

// A basis state has 1 at its index, an index past the qubits is rejected
static void test_create_basis(void) {
  QCS_State state = basis_state(3, 0b101);
  assert_basis(&state, 0b101);
  qcs_state_destroy(&state);

  assert(qcs_state_create_basis(3, 8, &state) == QCS_ERR_QUBIT_RANGE);
}

// Probabilities of each pattern add up to the norm
static void test_probability(void) {
  QCS_State state = new_state(1);
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  assert(qcs_eq(qcs_state_probability(&state, 0), 0.5));
  assert(qcs_eq(qcs_state_probability(&state, 1), 0.5));
  assert(qcs_eq(qcs_state_norm(&state), 1));
  qcs_state_destroy(&state);
}

// The same seed gives the same numbers, different seeds different ones
static void test_random_seed(void) {
  QCS_Random a = qcs_random_create(42);
  QCS_Random b = qcs_random_create(42);
  QCS_Random c = qcs_random_create(43);
  for (int k = 0; k < 100; k++) {
    uint64_t x = qcs_random_next(&a);
    assert(x == qcs_random_next(&b));
    assert(x != qcs_random_next(&c));
  }
}

// Destroy frees the amplitudes, twice is harmless
static void test_destroy(void) {
  QCS_State state = new_state(2);
  qcs_state_destroy(&state);
  assert(state.amps == NULL);
  qcs_state_destroy(&state);
}

// *** ONE QUBIT ***

static void test_single_qubit_gates(void) {
  // X flips |0⟩ to |1⟩
  QCS_State state = new_state(1);
  qcs_state_apply_gate(&state, qcs_gate_create_x(0));
  assert_amps(&state, (double complex[]){0, 1});
  qcs_state_destroy(&state);

  // H makes an equal superposition
  state = new_state(1);
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  assert_amps(&state, (double complex[]){S, S});

  // Z flips the sign of |1⟩ only
  qcs_state_apply_gate(&state, qcs_gate_create_z(0));
  assert_amps(&state, (double complex[]){S, -S});
  qcs_state_destroy(&state);

  // Y takes |0⟩ to i|1⟩
  state = new_state(1);
  qcs_state_apply_gate(&state, qcs_gate_create_y(0));
  assert_amps(&state, (double complex[]){0, I});
  qcs_state_destroy(&state);
}

// H twice is the identity: needs both amplitudes read before either is written
static void test_interference(void) {
  QCS_State state = new_state(1);
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  assert_basis(&state, 0);

  // H Z H = X: the phase flip turns into a bit flip
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  qcs_state_apply_gate(&state, qcs_gate_create_z(0));
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  assert_basis(&state, 1);
  qcs_state_destroy(&state);
}

// *** EVERY TARGET ***

// X on qubit t sets bit t of the index: qubit 0 is the least significant bit
static void test_x_every_target(void) {
  for (uint32_t t = 0; t < 3; t++) {
    QCS_State state = new_state(3);
    qcs_state_apply_gate(&state, qcs_gate_create_x(t));
    assert_basis(&state, (size_t)1 << t);
    qcs_state_destroy(&state);
  }
}

// H on qubit t mixes the pair (0, 2^t) and nothing else
static void test_h_every_target(void) {
  for (uint32_t t = 0; t < 3; t++) {
    QCS_State state = new_state(3);
    qcs_state_apply_gate(&state, qcs_gate_create_h(t));
    for (size_t i = 0; i < 8; i++) {
      double complex expected = (i == 0 || i == ((size_t)1 << t)) ? S : 0;
      assert(qcs_ceq(state.amps[i], expected));
    }
    qcs_state_destroy(&state);
  }
}

// H on every qubit gives all 2^n patterns the same amplitude
static void test_uniform_superposition(void) {
  QCS_State state = new_state(4);
  for (uint32_t q = 0; q < 4; q++) {
    qcs_state_apply_gate(&state, qcs_gate_create_h(q));
  }
  for (size_t i = 0; i < 16; i++) {
    assert(qcs_ceq(state.amps[i], 0.25));
  }
  qcs_state_destroy(&state);
}

// *** CONTROLS ***

// CX (control 0, target 1) flips qubit 1 exactly when qubit 0 is 1
static void test_cx_truth_table(void) {
  // input index → output index, bits written |q1 q0⟩
  size_t expected[4] = {
      [0b00] = 0b00,
      [0b01] = 0b11,
      [0b10] = 0b10,
      [0b11] = 0b01,
  };
  for (size_t in = 0; in < 4; in++) {
    QCS_State state = basis_state(2, in);
    qcs_state_apply_gate(&state, qcs_gate_create_cx(1, 0));
    assert_basis(&state, expected[in]);
    qcs_state_destroy(&state);
  }
}

// CCX (controls 0 and 1, target 2) flips qubit 2 only when both are 1
static void test_ccx_truth_table(void) {
  QCS_Gate ccx = qcs_gate_create_cx(2, 0);
  qcs_gate_add_control(&ccx, 1);
  for (size_t in = 0; in < 8; in++) {
    QCS_State state = basis_state(3, in);
    qcs_state_apply_gate(&state, ccx);
    size_t out = (in & 0b011) == 0b011 ? in ^ 0b100 : in;
    assert_basis(&state, out);
    qcs_state_destroy(&state);
  }
}

// CZ adds a -1 to |11⟩ only, whichever qubit is called the control
static void test_cz_symmetric(void) {
  QCS_Gate czs[] = {qcs_gate_create_cz(1, 0), qcs_gate_create_cz(0, 1)};
  for (size_t k = 0; k < 2; k++) {
    QCS_State state = new_state(2);
    qcs_state_apply_gate(&state, qcs_gate_create_h(0));
    qcs_state_apply_gate(&state, qcs_gate_create_h(1));
    qcs_state_apply_gate(&state, czs[k]);
    assert_amps(&state, (double complex[]){0.5, 0.5, 0.5, -0.5});
    qcs_state_destroy(&state);
  }
}

// *** CIRCUITS ***

// H, CX: (|00⟩ + |11⟩)/√2
static void test_bell(void) {
  QCS_Circuit bell;
  assert(qcs_circuit_create(2, &bell) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&bell, qcs_gate_create_h(0)) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&bell, qcs_gate_create_cx(1, 0)) == QCS_ERR_OK);

  QCS_State state = new_state(2);
  assert(qcs_circuit_run(&bell, &state) == QCS_ERR_OK);
  assert_amps(&state, (double complex[]){S, 0, 0, S});

  qcs_state_destroy(&state);
  qcs_circuit_destroy(&bell);
}

// H, then a CX chain: (|0…0⟩ + |1…1⟩)/√2, the first and last amplitudes
static void test_ghz(void) {
  for (uint32_t n = 2; n <= 10; n++) {
    QCS_Circuit ghz;
    assert(qcs_circuit_create(n, &ghz) == QCS_ERR_OK);
    assert(qcs_circuit_push_gate(&ghz, qcs_gate_create_h(0)) == QCS_ERR_OK);
    for (uint32_t q = 1; q < n; q++) {
      assert(qcs_circuit_push_gate(&ghz, qcs_gate_create_cx(q, q - 1)) ==
             QCS_ERR_OK);
    }

    QCS_State state = new_state(n);
    assert(qcs_circuit_run(&ghz, &state) == QCS_ERR_OK);
    size_t last = qcs_state_size(&state) - 1;
    for (size_t i = 0; i <= last; i++) {
      assert(qcs_ceq(state.amps[i], (i == 0 || i == last) ? S : 0));
    }

    qcs_state_destroy(&state);
    qcs_circuit_destroy(&ghz);
  }
}

// A circuit only runs on a state of its size, and leaves a wrong one alone
static void test_run_wrong_size(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(3, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_x(0)) == QCS_ERR_OK);

  QCS_State state = new_state(2);
  assert(qcs_circuit_run(&circuit, &state) == QCS_ERR_INVALID_STATE);
  assert_basis(&state, 0);

  qcs_state_destroy(&state);
  qcs_circuit_destroy(&circuit);
}

// *** INVARIANTS ***

// Unitary gates keep the total probability at 1
static void test_norm_random_circuit(void) {
  QCS_Random random = qcs_random_create(1);
  QCS_State state = new_state(10);
  for (int g = 0; g < 1000; g++) {
    qcs_state_apply_gate(&state, qcs_gate_create_random(10, &random));
  }
  assert(qcs_eq(qcs_state_norm(&state), 1));
  qcs_state_destroy(&state);
}

// A random circuit followed by its inverse (adjoint gates, reverse order)
// gives back |0…0⟩
static void test_inverse_circuit(void) {
  QCS_Random random = qcs_random_create(2);
  QCS_Gate gates[200];
  QCS_State state = new_state(6);
  for (int g = 0; g < 200; g++) {
    gates[g] = qcs_gate_create_random(6, &random);
    qcs_state_apply_gate(&state, gates[g]);
  }
  for (int g = 199; g >= 0; g--) {
    qcs_state_apply_gate(&state, qcs_gate_adjoint(gates[g]));
  }
  assert_basis(&state, 0);
  qcs_state_destroy(&state);
}

// No signalling: a gate on qubit t doesn't change the probability of any
// configuration of the other qubits
static void test_gate_is_local(void) {
  enum { N = 5, SIZE = 1 << N };
  QCS_Random random = qcs_random_create(3);
  QCS_State state = new_state(N);
  for (int g = 0; g < 100; g++) {
    qcs_state_apply_gate(&state, qcs_gate_create_random(N, &random));
  }

  for (uint32_t t = 0; t < N; t++) {
    size_t bit = (size_t)1 << t;
    // P(others = r), indexed by the pattern with bit t cleared
    double before[SIZE] = {0};
    for (size_t i = 0; i < SIZE; i++) {
      before[i & ~bit] += qcs_state_probability(&state, i);
    }

    qcs_state_apply_gate(&state, qcs_gate_create_h(t));

    double after[SIZE] = {0};
    for (size_t i = 0; i < SIZE; i++) {
      after[i & ~bit] += qcs_state_probability(&state, i);
    }
    for (size_t r = 0; r < SIZE; r++) {
      assert(qcs_eq(after[r], before[r]));
    }
  }
  qcs_state_destroy(&state);
}

// *** SAMPLING ***

#define SHOTS 10000

// counts[i] = shots that measured pattern i
static void count_outcomes(const size_t *outcomes, size_t shots, size_t *counts,
                           size_t size) {
  for (size_t i = 0; i < size; i++) {
    counts[i] = 0;
  }
  for (size_t s = 0; s < shots; s++) {
    assert(outcomes[s] < size);
    counts[outcomes[s]]++;
  }
}

// count within 5σ of a binomial(shots, p)
static bool near_expected(size_t count, size_t shots, double p) {
  double mean = shots * p;
  double sigma = sqrt(shots * p * (1 - p));
  return fabs((double)count - mean) <= 5 * sigma;
}

// Uniform doubles in [0, 1) with mean 1/2
static void test_random_double(void) {
  enum { DRAWS = 1000000 };
  QCS_Random random = qcs_random_create(4);
  double sum = 0;
  for (int k = 0; k < DRAWS; k++) {
    double u = qcs_random_next_double(&random);
    assert(u >= 0 && u < 1);
    sum += u;
  }
  // σ of the mean of uniforms: 1/√(12 N)
  assert(fabs(sum / DRAWS - 0.5) < 5 / sqrt(12.0 * DRAWS));
}

// Bell: only 00 and 11, about half each
static void test_sample_bell(void) {
  QCS_State state = new_state(2);
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  qcs_state_apply_gate(&state, qcs_gate_create_cx(1, 0));

  static size_t outcomes[SHOTS];
  QCS_Random random = qcs_random_create(5);
  assert(qcs_state_sample(&state, SHOTS, &random, outcomes) == QCS_ERR_OK);

  size_t counts[4];
  count_outcomes(outcomes, SHOTS, counts, 4);
  assert(counts[0b01] == 0 && counts[0b10] == 0);
  assert(near_expected(counts[0b00], SHOTS, 0.5));
  assert(near_expected(counts[0b11], SHOTS, 0.5));
  qcs_state_destroy(&state);
}

// A basis state always measures its own pattern
static void test_sample_basis(void) {
  QCS_State state = basis_state(3, 0b101);
  size_t outcomes[100];
  QCS_Random random = qcs_random_create(6);
  assert(qcs_state_sample(&state, 100, &random, outcomes) == QCS_ERR_OK);
  for (size_t s = 0; s < 100; s++) {
    assert(outcomes[s] == 0b101);
  }
  qcs_state_destroy(&state);
}

// Unequal probabilities: 0.9 and 0.1
static void test_sample_unequal(void) {
  QCS_State state = new_state(1);
  state.amps[0] = sqrt(0.9);
  state.amps[1] = sqrt(0.1);

  static size_t outcomes[SHOTS];
  QCS_Random random = qcs_random_create(7);
  assert(qcs_state_sample(&state, SHOTS, &random, outcomes) == QCS_ERR_OK);

  size_t counts[2];
  count_outcomes(outcomes, SHOTS, counts, 2);
  assert(near_expected(counts[0], SHOTS, 0.9));
  assert(near_expected(counts[1], SHOTS, 0.1));
  qcs_state_destroy(&state);
}

// H on every qubit: 16 patterns, 1/16 each
static void test_sample_uniform(void) {
  QCS_State state = new_state(4);
  for (uint32_t q = 0; q < 4; q++) {
    qcs_state_apply_gate(&state, qcs_gate_create_h(q));
  }

  static size_t outcomes[SHOTS];
  QCS_Random random = qcs_random_create(8);
  assert(qcs_state_sample(&state, SHOTS, &random, outcomes) == QCS_ERR_OK);

  size_t counts[16];
  count_outcomes(outcomes, SHOTS, counts, 16);
  for (size_t i = 0; i < 16; i++) {
    assert(near_expected(counts[i], SHOTS, 1.0 / 16));
  }
  qcs_state_destroy(&state);
}

// Same seed, same shots; another seed, other shots
static void test_sample_seed(void) {
  QCS_State state = new_state(4);
  for (uint32_t q = 0; q < 4; q++) {
    qcs_state_apply_gate(&state, qcs_gate_create_h(q));
  }

  size_t a[100], b[100], c[100];
  QCS_Random random_a = qcs_random_create(9);
  QCS_Random random_b = qcs_random_create(9);
  QCS_Random random_c = qcs_random_create(10);
  assert(qcs_state_sample(&state, 100, &random_a, a) == QCS_ERR_OK);
  assert(qcs_state_sample(&state, 100, &random_b, b) == QCS_ERR_OK);
  assert(qcs_state_sample(&state, 100, &random_c, c) == QCS_ERR_OK);

  bool differs = false;
  for (size_t s = 0; s < 100; s++) {
    assert(a[s] == b[s]);
    differs |= a[s] != c[s];
  }
  assert(differs);
  qcs_state_destroy(&state);
}

// A total probability slightly under 1 still samples every shot
static void test_sample_unnormalized(void) {
  // 00 and 01 only, total probability 0.81
  QCS_State state = new_state(2);
  qcs_state_apply_gate(&state, qcs_gate_create_h(0));
  for (size_t i = 0; i < 4; i++) {
    state.amps[i] *= 0.9;
  }

  static size_t outcomes[SHOTS];
  QCS_Random random = qcs_random_create(11);
  assert(qcs_state_sample(&state, SHOTS, &random, outcomes) == QCS_ERR_OK);

  size_t counts[4];
  count_outcomes(outcomes, SHOTS, counts, 4);
  assert(counts[0b10] == 0 && counts[0b11] == 0);
  assert(near_expected(counts[0b00], SHOTS, 0.5));
  assert(near_expected(counts[0b01], SHOTS, 0.5));
  qcs_state_destroy(&state);
}

// A state with no probability can't be sampled
static void test_sample_zero_state(void) {
  QCS_State state = new_state(1);
  state.amps[0] = 0;
  size_t outcome;
  QCS_Random random = qcs_random_create(12);
  assert(qcs_state_sample(&state, 1, &random, &outcome) ==
         QCS_ERR_INVALID_STATE);
  qcs_state_destroy(&state);
}

// *** FIDELITY ***

// F(a, b), which must be computed
static double fidelity(const QCS_State *a, const QCS_State *b) {
  double F;
  assert(qcs_state_fidelity(a, b, &F) == QCS_ERR_OK);
  return F;
}

// A state of n qubits after random gates, with complex amplitudes
static QCS_State random_circuit_state(uint32_t n, uint64_t seed) {
  QCS_Random random = qcs_random_create(seed);
  QCS_State state = new_state(n);
  for (int g = 0; g < 50; g++) {
    qcs_state_apply_gate(&state, qcs_gate_create_random(n, &random));
  }
  return state;
}

// A copy of state with every amplitude times factor
static QCS_State scaled(const QCS_State *state, double complex factor) {
  QCS_State out = new_state(state->n);
  for (size_t i = 0; i < qcs_state_size(state); i++) {
    out.amps[i] = factor * state->amps[i];
  }
  return out;
}

// A state against itself: 1
static void test_fidelity(void) {
  QCS_Random random = qcs_random_create(1);

  QCS_State state;
  assert(qcs_state_create_random(2, &random, &state) == QCS_ERR_OK);
  double F;
  assert(qcs_state_fidelity(&state, &state, &F) == QCS_ERR_OK);
  assert(qcs_eq(F, 1));
  qcs_state_destroy(&state);

  state = random_circuit_state(4, 13);
  assert(qcs_eq(fidelity(&state, &state), 1));
  qcs_state_destroy(&state);
}

// (|0⟩ + i|1⟩)/√2 against itself: 1, conj on a cancels the i
static void test_fidelity_conj(void) {
  QCS_State state = new_state(1);
  state.amps[0] = S;
  state.amps[1] = I * S;
  assert(qcs_eq(fidelity(&state, &state), 1));
  qcs_state_destroy(&state);
}

// A global phase doesn't change the state: -1 and i give 1
static void test_fidelity_global_phase(void) {
  QCS_State state = random_circuit_state(4, 14);
  double complex phases[] = {-1, I, cexp(I * 0.3)};
  for (size_t k = 0; k < sizeof phases / sizeof phases[0]; k++) {
    QCS_State other = scaled(&state, phases[k]);
    assert(qcs_eq(fidelity(&state, &other), 1));
    qcs_state_destroy(&other);
  }
  qcs_state_destroy(&state);
}

// Orthogonal states: 0, also Φ+ and Φ- with the same probabilities
static void test_fidelity_orthogonal(void) {
  QCS_State zero = basis_state(1, 0);
  QCS_State one = basis_state(1, 1);
  assert(qcs_eq(fidelity(&zero, &one), 0));
  qcs_state_destroy(&zero);
  qcs_state_destroy(&one);

  // Φ+ = (|00⟩ + |11⟩)/√2, Φ- = Z on qubit 0 of Φ+
  QCS_State plus = new_state(2);
  qcs_state_apply_gate(&plus, qcs_gate_create_h(0));
  qcs_state_apply_gate(&plus, qcs_gate_create_cx(1, 0));
  QCS_State minus = scaled(&plus, 1);
  qcs_state_apply_gate(&minus, qcs_gate_create_z(0));
  assert(qcs_eq(fidelity(&plus, &minus), 0));
  qcs_state_destroy(&plus);
  qcs_state_destroy(&minus);
}

// |0⟩ against H|0⟩: 1/2, the same both ways
static void test_fidelity_overlap(void) {
  QCS_State zero = new_state(1);
  QCS_State plus = new_state(1);
  qcs_state_apply_gate(&plus, qcs_gate_create_h(0));
  assert(qcs_eq(fidelity(&zero, &plus), 0.5));
  assert(qcs_eq(fidelity(&plus, &zero), 0.5));
  qcs_state_destroy(&zero);
  qcs_state_destroy(&plus);

  QCS_State a = random_circuit_state(4, 15);
  QCS_State b = random_circuit_state(4, 16);
  assert(qcs_eq(fidelity(&a, &b), fidelity(&b, &a)));
  qcs_state_destroy(&a);
  qcs_state_destroy(&b);
}

// The norms are divided out: 0.9 H|0⟩ against H|0⟩ is 1, against |0⟩ 1/2
static void test_fidelity_unnormalized(void) {
  QCS_State zero = new_state(1);
  QCS_State plus = new_state(1);
  qcs_state_apply_gate(&plus, qcs_gate_create_h(0));
  QCS_State short_plus = scaled(&plus, 0.9);
  assert(qcs_eq(fidelity(&plus, &short_plus), 1));
  assert(qcs_eq(fidelity(&zero, &short_plus), 0.5));
  qcs_state_destroy(&zero);
  qcs_state_destroy(&plus);
  qcs_state_destroy(&short_plus);
}

// States of different sizes and zero states have no fidelity
static void test_fidelity_invalid(void) {
  QCS_State one_qubit = new_state(1);
  QCS_State two_qubits = new_state(2);
  QCS_State zero_state = scaled(&one_qubit, 0);
  double F;
  assert(qcs_state_fidelity(&one_qubit, &two_qubits, &F) ==
         QCS_ERR_INVALID_STATE);
  assert(qcs_state_fidelity(&one_qubit, &zero_state, &F) ==
         QCS_ERR_INVALID_STATE);
  assert(qcs_state_fidelity(&zero_state, &one_qubit, &F) ==
         QCS_ERR_INVALID_STATE);
  qcs_state_destroy(&one_qubit);
  qcs_state_destroy(&two_qubits);
  qcs_state_destroy(&zero_state);
}

int main(void) {
  test_create();
  test_create_max_qubits();
  test_create_random();
  test_create_basis();
  test_probability();
  test_random_seed();
  test_destroy();
  test_single_qubit_gates();
  test_interference();
  test_x_every_target();
  test_h_every_target();
  test_uniform_superposition();
  test_cx_truth_table();
  test_ccx_truth_table();
  test_cz_symmetric();
  test_bell();
  test_ghz();
  test_run_wrong_size();
  test_norm_random_circuit();
  test_inverse_circuit();
  test_gate_is_local();
  test_random_double();
  test_sample_bell();
  test_sample_basis();
  test_sample_unequal();
  test_sample_uniform();
  test_sample_seed();
  test_sample_unnormalized();
  test_sample_zero_state();
  test_fidelity();
  test_fidelity_conj();
  test_fidelity_global_phase();
  test_fidelity_orthogonal();
  test_fidelity_overlap();
  test_fidelity_unnormalized();
  test_fidelity_invalid();
  puts("state ok");
  return 0;
}
