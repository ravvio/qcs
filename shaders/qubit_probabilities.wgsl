// Sums the probabilities of a qubit being 0 and 1, one sum per workgroup

const workgroup_size = 64u;

struct Measure {
    // Qubit whose probabilities are summed
    qubit: u32,
}

@group(0) @binding(0) var<uniform> measure: Measure;
@group(0) @binding(1) var<storage, read_write> state: array<vec2f>;
// (p0, p1) of each workgroup's amplitudes
@group(0) @binding(2) var<storage, read_write> sums: array<vec2f>;

var<workgroup> partial: array<vec2f, workgroup_size>;

@compute @workgroup_size(workgroup_size)
fn comp_main(@builtin(workgroup_id) group: vec3u,
             @builtin(num_workgroups) groups: vec3u,
             @builtin(local_invocation_index) local: u32) {
    let g = group.y * groups.x + group.x;
    let i = g * workgroup_size + local;

    // this amplitude's probability, as p0 or p1
    var p = vec2f(0.0);
    if (i < arrayLength(&state)) {
        let probability = dot(state[i], state[i]);
        if (((i >> measure.qubit) & 1u) == 1u) {
            p.y = probability;
        } else {
            p.x = probability;
        }
    }

    // sum the workgroup's, halving each step
    partial[local] = p;
    workgroupBarrier();
    for (var s = workgroup_size / 2u; s > 0u; s = s / 2u) {
        if (local < s) {
            partial[local] += partial[local + s];
        }
        workgroupBarrier();
    }
    if (local == 0u && g < arrayLength(&sums)) {
        sums[g] = partial[0];
    }
}
