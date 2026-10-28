source_filename = "lifetime"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture)
define void @lifetimes() {
entry:
 %small = alloca [63 x i8]
 %large = alloca [64 x i8]
 call void @llvm.lifetime.start.p0(i64 63, ptr %small)
 call void @llvm.lifetime.start.p0(i64 64, ptr %large)
 call void @llvm.lifetime.end.p0(i64 64, ptr %large)
 call void @llvm.lifetime.end.p0(i64 63, ptr %small)
 ret void
}
