#include <qcs/qcs.h>
#include <stdio.h>
#include <stdlib.h>

// qcs [qubits]: starts the GPU with room for that many qubits
int main(int argc, char **argv) {
  uint32_t qubits = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
  if (qcs_gpu_init(qubits) != QCS_ERR_OK) {
    return 1;
  }
  fprintf(stderr, "[i] GPU: up to %u qubits\n", qcs_gpu_max_qubits());
  qcs_gpu_terminate();
  return 0;
}
