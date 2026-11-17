// Simulator a program runs on: what the shot-branching executor does to its
// states
#ifndef QCS_SIMULATOR_DECL
#define QCS_SIMULATOR_DECL

#include <qcs/program.h>

// States are opaque: a QCS_State on the CPU, a QCS_GPUState on the GPU
typedef struct {
  void *context; // the GPU's shaders, NULL on the CPU
  // a state of n qubits at |0...0⟩, or NULL
  void *(*create)(void *context, uint32_t n);
  // a copy of state, or NULL
  void *(*copy)(void *context, const void *state);
  void (*destroy)(void *context, void *state);
  QCS_Error (*run)(void *context, void *state, const QCS_Circuit *circuit);
  QCS_Error (*qubit_probabilities)(void *context, void *state, uint32_t qubit,
                                   double p[2]);
  // collapse qubit onto value, p its probabilities of 0 and 1
  QCS_Error (*collapse)(void *context, void *state, uint32_t qubit, bool value,
                        const double p[2]);
  QCS_Error (*sample)(void *context, void *state, size_t shots,
                      QCS_Random *random, size_t *outcomes);
} QCS_Simulator;

// run program on simulator shots times, each shot's clbits into clbits,
// grouped by branch
QCS_Error qcs_simulator_run_program(const QCS_Simulator *simulator,
                                    const QCS_Program *program, size_t shots,
                                    QCS_Random *random, uint64_t *clbits);

#endif
