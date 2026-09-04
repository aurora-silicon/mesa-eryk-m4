target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v27-apple-macosx15.0.0"

declare void @llvm.agx2.imgwblk.coords.2d(i16, i16, i32 addrspace(64)*, i8 addrspace(1)*, <2 x i32>)
define void @rt_tri_oneshot(<2 x i32> %gid) local_unnamed_addr {
entry:
  %coords = add <2 x i32> %gid, zeroinitializer
  call void @llvm.agx2.imgwblk.coords.2d(i16 4, i16 0, i32 addrspace(64)* null, i8 addrspace(1)* null, <2 x i32> %coords)
  ret void
}

!air.kernel = !{!0}
!air.version = !{!17}
!air.language_version = !{!18}
!0 = !{void (<2 x i32>)* @rt_tri_oneshot, !1, !2}
!1 = !{}
!2 = !{!3}
!3 = !{i32 0, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint2", !"air.arg_name", !"gid"}
!17 = !{i32 2, i32 7, i32 0}
!18 = !{!"Metal", i32 3, i32 2, i32 0}
