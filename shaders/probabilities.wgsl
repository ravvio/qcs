// Computes state amplitude probabilities

const workgroup_size = 64u;

@group(0) @binding(1) var<storage, read_write> state: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> probs: array<f32>;

@compute @workgroup_size(workgroup_size)
fn comp_main(@builtin(workgroup_id) group: vec3u,
             @builtin(num_workgroups) groups: vec3u,
             @builtin(local_invocation_index) local: u32) {
    let i = (group.y * groups.x + group.x) * workgroup_size + local;
    if (i >= arrayLength(&probs)) {
        return;
    }
    probs[i] = dot(state[i], state[i]);
}
