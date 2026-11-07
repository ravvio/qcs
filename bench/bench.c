// Time per gate and effective bandwidth of the CPU or GPU simulator:
// bench cpu|gpu [qubits|min-max] [gates]

#include <math.h>
#include <qcs/qcs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Seconds on a monotonic clock
static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// H on gate_count random targets, so every pair is read and written
static QCS_Circuit h_circuit(uint32_t n, uint32_t gate_count,
                             QCS_Random *random) {
  QCS_Circuit circuit;
  if (qcs_circuit_create(n, &circuit) != QCS_ERR_OK) {
    exit(1);
  }
  for (uint32_t g = 0; g < gate_count; g++) {
    uint32_t target = (uint32_t)(qcs_random_next(random) % n);
    qcs_circuit_push_gate(&circuit, qcs_gate_create_h(target));
  }
  return circuit;
}

// Best of reps runs of circuit on the CPU, in seconds
static double time_cpu(const QCS_Circuit *circuit, int reps) {
  QCS_State state;
  if (qcs_state_create(circuit->qubit_count, &state) != QCS_ERR_OK) {
    exit(1);
  }
  double best = INFINITY;
  for (int r = 0; r < reps; r++) {
    double start = now();
    qcs_circuit_run(circuit, &state);
    double t = now() - start;
    best = t < best ? t : best;
  }
  qcs_state_destroy(&state);
  return best;
}

// Best of reps runs of circuit on the GPU plus a 4-byte readback, in seconds
static double time_gpu(const QCS_Circuit *circuit, int reps) {
  QCS_GPUState state;
  if (qcs_gpu_state_create(circuit->qubit_count, &state) != QCS_ERR_OK) {
    exit(1);
  }
  double best = INFINITY;
  // first run untimed: warm up
  for (int r = 0; r <= reps; r++) {
    double start = now();
    qcs_gpu_circuit_run(circuit, &state);
    // waits for the GPU
    float amp;
    if (gfx_storage_buffer_read(&state.amps, 0, &amp, sizeof amp) !=
        GFX_ERR_OK) {
      exit(1);
    }
    double t = now() - start;
    best = r > 0 && t < best ? t : best;
  }
  qcs_gpu_state_destroy(&state);
  return best;
}

int main(int argc, char **argv) {
  bool gpu = argc > 1 && strcmp(argv[1], "gpu") == 0;
  if (argc < 2 || (!gpu && strcmp(argv[1], "cpu") != 0)) {
    fprintf(stderr, "usage: bench cpu|gpu [qubits|min-max] [gates]\n");
    return 1;
  }
  // qubits: one count or a range, 16-24 by default
  uint32_t min = 16, max = 24;
  if (argc > 2) {
    char *end;
    min = max = (uint32_t)strtoul(argv[2], &end, 10);
    if (*end == '-') {
      max = (uint32_t)strtoul(end + 1, NULL, 10);
    }
  }
  if (min == 0 || min > max || max > QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] qubits must be in 1..%u, min <= max\n",
            QCS_MAX_QUBITS);
    return 1;
  }
  uint32_t gate_count = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 100;
  if (gate_count == 0) {
    fprintf(stderr, "[e] gates must be at least 1\n");
    return 1;
  }
  if (gpu && qcs_gpu_init(max) != QCS_ERR_OK) {
    return 1;
  }

  // per backend: timed runs, bytes per amplitude
  int reps = gpu ? 5 : 1;
  double amp_size = gpu ? 8 : 16;
  double (*time)(const QCS_Circuit *, int) = gpu ? time_gpu : time_cpu;

  QCS_Random random = qcs_random_create(1);
  printf("%s\nqubits  ms/gate   GB/s\n", argv[1]);
  for (uint32_t n = min; n <= max; n++) {
    QCS_Circuit empty = h_circuit(n, 0, &random);
    QCS_Circuit circuit = h_circuit(n, gate_count, &random);

    // fixed cost per run (shader, submit, readback), then the gates on top
    double fixed = time(&empty, reps);
    double gate = (time(&circuit, reps) - fixed) / gate_count;
    // each gate reads and writes all 2^n amplitudes
    double bytes = 2 * amp_size * (double)((uint64_t)1 << n);
    printf("%6u  %7.3f  %5.1f\n", n, gate * 1e3, bytes / gate / 1e9);

    qcs_circuit_destroy(&circuit);
    qcs_circuit_destroy(&empty);
  }

  if (gpu) {
    qcs_gpu_terminate();
  }
  return 0;
}
