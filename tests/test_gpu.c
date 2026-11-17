// Checks the GPU qubit ceiling, state and circuit runs against the CPU, needs
// a WebGPU adapter

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <stdlib.h>

#define S (1.0 / sqrt(2.0))

// circuit run on the GPU from |0...0>, read back
static QCS_State run_gpu(const QCS_Circuit *circuit) {
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(circuit->qubit_count, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(circuit, &gpu) == QCS_ERR_OK);
  QCS_State state;
  assert(qcs_gpu_state_amplitudes(&gpu, &state) == QCS_ERR_OK);
  qcs_gpu_state_destroy(&gpu);
  return state;
}

// shots outcomes counted per pattern into counts, size patterns
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

// |⟨ψ_cpu|ψ_gpu⟩|² of circuit run on both from |0...0>
static double cpu_gpu_fidelity(const QCS_Circuit *circuit) {
  QCS_State cpu;
  assert(qcs_state_create(circuit->qubit_count, &cpu) == QCS_ERR_OK);
  assert(qcs_circuit_run(circuit, &cpu) == QCS_ERR_OK);
  QCS_State gpu = run_gpu(circuit);
  double F;
  assert(qcs_state_fidelity(&cpu, &gpu, &F) == QCS_ERR_OK);
  qcs_state_destroy(&cpu);
  qcs_state_destroy(&gpu);
  return F;
}

// gate_count random gates on n qubits
static QCS_Circuit random_circuit(uint32_t n, int gate_count,
                                  QCS_Random *random) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(n, &circuit) == QCS_ERR_OK);
  for (int g = 0; g < gate_count; g++) {
    assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_random(n, random)) ==
           QCS_ERR_OK);
  }
  return circuit;
}

// a new state is |0...0>: amplitude 0 is 1, the others 0
static void test_state_create(uint32_t n) {
  QCS_GPUState state;
  assert(qcs_gpu_state_create(n, &state) == QCS_ERR_OK);
  assert(state.n == n);

  // (re, im) per amplitude
  float amps[2 << 3];
  size_t size = (size_t)1 << n;
  assert(2 * size <= sizeof amps / sizeof amps[0]);
  assert(gfx_storage_buffer_read(&state.amps, 0, amps,
                                 2 * size * sizeof(float)) == GFX_ERR_OK);
  for (size_t i = 0; i < size; i++) {
    assert(qcs_f32_near(amps[2 * i], i == 0 ? 1.0 : 0.0));
    assert(qcs_f32_near(amps[2 * i + 1], 0.0));
  }
  qcs_gpu_state_destroy(&state);
}

// more qubits than the device fits: refused
static void test_state_too_big(void) {
  QCS_GPUState state;
  assert(qcs_gpu_state_create(qcs_gpu_max_qubits() + 1, &state) ==
         QCS_ERR_MAX_QUBITS);
}

// n = 1, one pair in one workgroup: X|0> = |1>, H|0> = |+>
static void test_run_one_qubit(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(1, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_x(0)) == QCS_ERR_OK);
  QCS_State state = run_gpu(&circuit);
  assert(qcs_c64_near(state.amps[0], 0) && qcs_c64_near(state.amps[1], 1));
  qcs_state_destroy(&state);

  qcs_circuit_clear(&circuit);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_h(0)) == QCS_ERR_OK);
  state = run_gpu(&circuit);
  assert(qcs_c64_near(state.amps[0], S) && qcs_c64_near(state.amps[1], S));
  qcs_state_destroy(&state);
  qcs_circuit_destroy(&circuit);
}

// GHZ(n): H 0, CX 0→i: (|0...0> + |1...1>)/√2, n = 2 is Bell
static void test_run_ghz(uint32_t n) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(n, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_h(0)) == QCS_ERR_OK);
  for (uint32_t i = 1; i < n; i++) {
    assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_cx(i, 0)) ==
           QCS_ERR_OK);
  }
  QCS_State state = run_gpu(&circuit);
  size_t last = qcs_state_size(&state) - 1;
  for (size_t i = 0; i <= last; i++) {
    assert(qcs_c64_near(state.amps[i], i == 0 || i == last ? S : 0));
  }
  qcs_state_destroy(&state);
  qcs_circuit_destroy(&circuit);
}

// 100 seeded random circuits, n = 1..16, depth 50: same state as the CPU
static void test_run_random(void) {
  QCS_Random random = qcs_random_create(42);
  for (uint32_t c = 0; c < 100; c++) {
    uint32_t n = 1 + c % 16;
    QCS_Circuit circuit = random_circuit(n, 50, &random);
    double F = cpu_gpu_fidelity(&circuit);
    if (!(F >= 1 - 1e-5)) {
      fprintf(stderr, "[e] circuit %u, n = %u: fidelity %.9f\n", c, n, F);
      assert(false);
    }
    qcs_circuit_destroy(&circuit);
  }
}

// n = 23: 2^22 pairs / 64 = 65536 workgroups, one past the dimension limit,
// so the dispatch is split in 2D
static void test_run_split(void) {
  uint32_t n = 23;
  if (qcs_gpu_max_qubits() < n) {
    fprintf(stderr, "[i] skipping the 2D split: device fits %u qubits\n",
            qcs_gpu_max_qubits());
    return;
  }
  QCS_Random random = qcs_random_create(7);
  QCS_Circuit circuit = random_circuit(n, 20, &random);
  assert(cpu_gpu_fidelity(&circuit) >= 1 - 1e-5);
  qcs_circuit_destroy(&circuit);
}

// the global phase on every amplitude, as on the CPU (f32 precision)
static void test_run_global_phase(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(1, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_h(0)) == QCS_ERR_OK);
  circuit.global_phase = 0.3;
  QCS_State state = run_gpu(&circuit);
  double complex amp = cexp(0.3 * I) * S;
  assert(cabs(state.amps[0] - amp) < 1e-6);
  assert(cabs(state.amps[1] - amp) < 1e-6);
  qcs_state_destroy(&state);
  qcs_circuit_destroy(&circuit);
}

// a circuit for other qubits than the state: refused
static void test_run_mismatch(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(3, &circuit) == QCS_ERR_OK);
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(2, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(&circuit, &gpu) == QCS_ERR_INVALID_STATE);
  qcs_gpu_state_destroy(&gpu);
  qcs_circuit_destroy(&circuit);
}

// |a|² of random circuits, n = 1..16: same as the CPU, summing to 1
static void test_probabilities_random(void) {
  QCS_Random random = qcs_random_create(11);
  for (uint32_t c = 0; c < 32; c++) {
    uint32_t n = 1 + c % 16;
    QCS_Circuit circuit = random_circuit(n, 50, &random);
    QCS_State cpu;
    assert(qcs_state_create(n, &cpu) == QCS_ERR_OK);
    assert(qcs_circuit_run(&circuit, &cpu) == QCS_ERR_OK);

    QCS_GPUState gpu;
    assert(qcs_gpu_state_create(n, &gpu) == QCS_ERR_OK);
    assert(qcs_gpu_circuit_run(&circuit, &gpu) == QCS_ERR_OK);
    size_t size = qcs_state_size(&cpu);
    float *probs = malloc(size * sizeof(float));
    assert(probs);
    assert(qcs_gpu_state_probabilities(&gpu, probs) == QCS_ERR_OK);

    double sum = 0;
    for (size_t i = 0; i < size; i++) {
      assert(qcs_f32_near(probs[i], qcs_state_probability(&cpu, i)));
      sum += probs[i];
    }
    assert(fabs(sum - 1) < 1e-5);

    free(probs);
    qcs_gpu_state_destroy(&gpu);
    qcs_state_destroy(&cpu);
    qcs_circuit_destroy(&circuit);
  }
}

// n = 0: one amplitude, probability 1
static void test_probabilities_zero_qubits(void) {
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(0, &gpu) == QCS_ERR_OK);
  float probs[1] = {0};
  assert(qcs_gpu_state_probabilities(&gpu, probs) == QCS_ERR_OK);
  assert(qcs_f32_near(probs[0], 1));
  qcs_gpu_state_destroy(&gpu);
}

// X on qubits 0 and 2 of 3: every shot measures 101
static void test_sample_basis(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(3, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_x(0)) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_x(2)) == QCS_ERR_OK);
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(3, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(&circuit, &gpu) == QCS_ERR_OK);

  size_t outcomes[1000];
  QCS_Random random = qcs_random_create(3);
  assert(qcs_gpu_state_sample(&gpu, 1000, &random, outcomes) == QCS_ERR_OK);
  for (size_t s = 0; s < 1000; s++) {
    assert(outcomes[s] == 0b101);
  }
  qcs_gpu_state_destroy(&gpu);
  qcs_circuit_destroy(&circuit);
}

// Bell: only 00 and 11, 5000 ± 300 of 10000 each (σ = 50)
static void test_sample_bell(void) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(2, &circuit) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_h(0)) == QCS_ERR_OK);
  assert(qcs_circuit_push_gate(&circuit, qcs_gate_create_cx(1, 0)) ==
         QCS_ERR_OK);
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(2, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(&circuit, &gpu) == QCS_ERR_OK);

  static size_t outcomes[10000];
  QCS_Random random = qcs_random_create(5);
  assert(qcs_gpu_state_sample(&gpu, 10000, &random, outcomes) == QCS_ERR_OK);
  size_t counts[4];
  count_outcomes(outcomes, 10000, counts, 4);
  assert(counts[0b01] == 0 && counts[0b10] == 0);
  assert(counts[0b00] > 4700 && counts[0b00] < 5300);
  assert(counts[0b11] > 4700 && counts[0b11] < 5300);
  qcs_gpu_state_destroy(&gpu);
  qcs_circuit_destroy(&circuit);
}

// circuit on n qubits of the gates
static QCS_Circuit circuit_of(uint32_t n, const QCS_Gate *gates, size_t count) {
  QCS_Circuit circuit;
  assert(qcs_circuit_create(n, &circuit) == QCS_ERR_OK);
  for (size_t g = 0; g < count; g++) {
    assert(qcs_circuit_push_gate(&circuit, gates[g]) == QCS_ERR_OK);
  }
  return circuit;
}

// Measure: a basis state gives its bit and stays; |+⟩ collapses onto the
// value drawn
static void test_measure(void) {
  QCS_Circuit x = circuit_of(2, (QCS_Gate[]){qcs_gate_create_x(1)}, 1);
  QCS_GPUState gpu;
  assert(qcs_gpu_state_create(2, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(&x, &gpu) == QCS_ERR_OK);
  QCS_Random random = qcs_random_create(3);
  bool value;
  assert(qcs_gpu_state_measure(&gpu, 1, &random, &value) == QCS_ERR_OK);
  assert(value == 1);
  assert(qcs_gpu_state_measure(&gpu, 0, &random, &value) == QCS_ERR_OK);
  assert(value == 0);
  QCS_State state;
  assert(qcs_gpu_state_amplitudes(&gpu, &state) == QCS_ERR_OK);
  assert(qcs_c128_near(state.amps[0b10], 1));
  qcs_state_destroy(&state);
  qcs_gpu_state_destroy(&gpu);
  qcs_circuit_destroy(&x);

  // |+⟩: the drawn value's amplitude, scaled to 1
  QCS_Circuit h = circuit_of(1, (QCS_Gate[]){qcs_gate_create_h(0)}, 1);
  assert(qcs_gpu_state_create(1, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_circuit_run(&h, &gpu) == QCS_ERR_OK);
  assert(qcs_gpu_state_measure(&gpu, 0, &random, &value) == QCS_ERR_OK);
  assert(qcs_gpu_state_amplitudes(&gpu, &state) == QCS_ERR_OK);
  assert(qcs_c128_near(state.amps[value], 1));
  assert(qcs_c128_near(state.amps[!value], 0));
  qcs_state_destroy(&state);

  // out of range, and a value of probability 0
  assert(qcs_gpu_state_measure(&gpu, 1, &random, &value) ==
         QCS_ERR_QUBIT_RANGE);
  assert(qcs_gpu_state_collapse(&gpu, 0, !value) == QCS_ERR_INVALID_STATE);
  qcs_gpu_state_destroy(&gpu);
  qcs_circuit_destroy(&h);
}

// Teleportation with every forced (c0, c1): the GPU branch against the CPU's,
// and q[2] undone to |0⟩
static void test_collapse_teleport(void) {
  const double pi = QCS_PI;
  QCS_Gate prefix_gates[] = {
      qcs_gate_create_ry(0, pi / 3), qcs_gate_create_rz(0, pi / 4),
      qcs_gate_create_h(1),          qcs_gate_create_cx(2, 1),
      qcs_gate_create_cx(1, 0),      qcs_gate_create_h(0),
  };
  QCS_Circuit prefix = circuit_of(3, prefix_gates, 6);
  for (int c = 0; c < 4; c++) {
    bool c0 = c & 1, c1 = c >> 1 & 1;

    // the corrections, then the preparation undone
    QCS_Gate fix_gates[4];
    size_t count = 0;
    if (c1) {
      fix_gates[count++] = qcs_gate_create_x(2);
    }
    if (c0) {
      fix_gates[count++] = qcs_gate_create_z(2);
    }
    fix_gates[count++] = qcs_gate_create_rz(2, -pi / 4);
    fix_gates[count++] = qcs_gate_create_ry(2, -pi / 3);
    QCS_Circuit fix = circuit_of(3, fix_gates, count);

    // CPU
    QCS_State cpu;
    assert(qcs_state_create(3, &cpu) == QCS_ERR_OK);
    assert(qcs_circuit_run(&prefix, &cpu) == QCS_ERR_OK);
    assert(qcs_state_collapse(&cpu, 0, c0) == QCS_ERR_OK);
    assert(qcs_state_collapse(&cpu, 1, c1) == QCS_ERR_OK);
    assert(qcs_circuit_run(&fix, &cpu) == QCS_ERR_OK);

    // GPU
    QCS_GPUState gpu;
    assert(qcs_gpu_state_create(3, &gpu) == QCS_ERR_OK);
    assert(qcs_gpu_circuit_run(&prefix, &gpu) == QCS_ERR_OK);
    assert(qcs_gpu_state_collapse(&gpu, 0, c0) == QCS_ERR_OK);
    assert(qcs_gpu_state_collapse(&gpu, 1, c1) == QCS_ERR_OK);
    assert(qcs_gpu_circuit_run(&fix, &gpu) == QCS_ERR_OK);
    QCS_State back;
    assert(qcs_gpu_state_amplitudes(&gpu, &back) == QCS_ERR_OK);

    // same branch, q[2] at 0
    double F;
    assert(qcs_state_fidelity(&cpu, &back, &F) == QCS_ERR_OK);
    if (F < 1 - 1e-5) {
      fprintf(stderr, "[e] (c0, c1) = (%d, %d): fidelity %.9f\n", c0, c1, F);
      assert(false);
    }
    for (size_t i = 0b100; i < 8; i++) {
      assert(qcs_state_probability(&back, i) < 1e-5);
    }
    qcs_state_destroy(&back);
    qcs_gpu_state_destroy(&gpu);
    qcs_state_destroy(&cpu);
    qcs_circuit_destroy(&fix);
  }
  qcs_circuit_destroy(&prefix);
}

enum { SHOTS = 1000 };

// count within 5σ of a binomial(SHOTS, p)
static bool near_expected(size_t count, double p) {
  return fabs((double)count - SHOTS * p) <= 5 * sqrt(SHOTS * p * (1 - p));
}

// append gate to program, as a circuit on all its qubits
static void append_gate(QCS_Program *program, QCS_Gate gate) {
  QCS_Circuit circuit = circuit_of(program->qubit_count, &gate, 1);
  qcs_program_append_circuit(program, &circuit);
  qcs_circuit_destroy(&circuit);
}

// an op list holding gate, as a circuit on n qubits
static QCS_Op *ops_of_gate(uint32_t n, QCS_Gate gate) {
  QCS_Circuit circuit = circuit_of(n, &gate, 1);
  QCS_Op *ops = nullptr;
  qcs_ops_append_circuit(&ops, &circuit);
  qcs_circuit_destroy(&circuit);
  return ops;
}

// run program SHOTS times on the GPU, count each clbits value (below 8),
// destroy it
static void gpu_counts(QCS_Program *program, size_t counts[8]) {
  static uint64_t clbits[SHOTS];
  QCS_Random random = qcs_random_create(7);
  assert(qcs_gpu_program_run(program, SHOTS, &random, clbits) == QCS_ERR_OK);
  for (size_t k = 0; k < 8; k++) {
    counts[k] = 0;
  }
  for (size_t s = 0; s < SHOTS; s++) {
    assert(clbits[s] < 8);
    counts[clbits[s]]++;
  }
  qcs_program_destroy(program);
}

// The executor on the GPU: sampled, per shot, reset, if, teleportation
static void test_program_run(void) {
  size_t counts[8];

  // Bell, measured at the end: sampled
  QCS_Program program = qcs_program_create(2, 2);
  append_gate(&program, qcs_gate_create_h(0));
  append_gate(&program, qcs_gate_create_cx(1, 0));
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  assert(qcs_program_append_measure(&program, 1, 1) == QCS_ERR_OK);
  gpu_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5) && near_expected(counts[0b11], 0.5));
  assert(counts[0b01] == 0 && counts[0b10] == 0);

  // x after measuring its qubit: c1 = !c0
  program = qcs_program_create(1, 2);
  append_gate(&program, qcs_gate_create_h(0));
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  append_gate(&program, qcs_gate_create_x(0));
  assert(qcs_program_append_measure(&program, 0, 1) == QCS_ERR_OK);
  gpu_counts(&program, counts);
  assert(near_expected(counts[0b01], 0.5) && near_expected(counts[0b10], 0.5));
  assert(counts[0b00] == 0 && counts[0b11] == 0);

  // reset of |1⟩
  program = qcs_program_create(1, 1);
  append_gate(&program, qcs_gate_create_x(0));
  assert(qcs_program_append_reset(&program, 0) == QCS_ERR_OK);
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  gpu_counts(&program, counts);
  assert(counts[0] == SHOTS);

  // if (c0) x q0: c1 always 0
  program = qcs_program_create(1, 2);
  append_gate(&program, qcs_gate_create_h(0));
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  QCS_Op *then_ops = ops_of_gate(1, qcs_gate_create_x(0));
  assert(qcs_program_append_if(&program, 1, 1, &then_ops, nullptr) ==
         QCS_ERR_OK);
  assert(qcs_program_append_measure(&program, 0, 1) == QCS_ERR_OK);
  gpu_counts(&program, counts);
  assert(near_expected(counts[0b00], 0.5) && near_expected(counts[0b01], 0.5));
  assert(counts[0b10] == 0 && counts[0b11] == 0);

  // teleport |1⟩: q2 always 1, (c0, c1) a quarter each
  program = qcs_program_create(3, 3);
  append_gate(&program, qcs_gate_create_x(0));
  append_gate(&program, qcs_gate_create_h(1));
  append_gate(&program, qcs_gate_create_cx(2, 1));
  append_gate(&program, qcs_gate_create_cx(1, 0));
  append_gate(&program, qcs_gate_create_h(0));
  assert(qcs_program_append_measure(&program, 0, 0) == QCS_ERR_OK);
  assert(qcs_program_append_measure(&program, 1, 1) == QCS_ERR_OK);
  QCS_Op *fix_x = ops_of_gate(3, qcs_gate_create_x(2));
  QCS_Op *fix_z = ops_of_gate(3, qcs_gate_create_z(2));
  assert(qcs_program_append_if(&program, 0b10, 0b10, &fix_x, nullptr) ==
         QCS_ERR_OK);
  assert(qcs_program_append_if(&program, 0b01, 0b01, &fix_z, nullptr) ==
         QCS_ERR_OK);
  assert(qcs_program_append_measure(&program, 2, 2) == QCS_ERR_OK);
  gpu_counts(&program, counts);
  for (size_t k = 0; k < 4; k++) {
    assert(counts[k] == 0);
    assert(near_expected(counts[0b100 | k], 0.25));
  }
}

int main(void) {
  // more qubits than any GPU fits: refused before init
  assert(qcs_gpu_init(QCS_MAX_QUBITS + 1) == QCS_ERR_MAX_QUBITS);

  // room for 20 qubits: 8 MiB, within the defaults
  assert(qcs_gpu_init(20) == QCS_ERR_OK);
  assert(qcs_gpu_max_qubits() >= 20);

  for (uint32_t n = 0; n <= 3; n++) {
    test_state_create(n);
  }
  test_state_too_big();

  test_run_one_qubit();
  for (uint32_t n = 2; n <= 10; n++) {
    test_run_ghz(n);
  }
  test_run_random();
  test_run_split();
  test_run_global_phase();
  test_run_mismatch();

  test_probabilities_random();
  test_probabilities_zero_qubits();
  test_sample_basis();
  test_sample_bell();

  test_measure();
  test_collapse_teleport();
  test_program_run();
  qcs_gpu_terminate();

  puts("gpu ok");
  return 0;
}
