#include <metal_stdlib>
using namespace metal;
kernel void cs(device uint *input [[buffer(0)]],device atomic_uint *output [[buffer(1)]],uint id [[thread_position_in_grid]]) {
 uint total=0;
 for(uint j=0;j<input[id];j++){
 if(input[id+192]>j){atomic_fetch_add_explicit(output+id,1u,memory_order_relaxed);if(input[id+128]>j){atomic_fetch_add_explicit(output+id,1u,memory_order_relaxed);if(input[id+64]==j){break;}atomic_fetch_add_explicit(output+id,3u,memory_order_relaxed);}atomic_fetch_add_explicit(output+id,3u,memory_order_relaxed);}
 total+=j*7u;
 }
 atomic_fetch_add_explicit(output+id,total,memory_order_relaxed);
}
