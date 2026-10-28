; ModuleID = '/home/weicresec/Desktop/stateafl/tests/llvm18/ir_fixture.c'
source_filename = "/home/weicresec/Desktop/stateafl/tests/llvm18/ir_fixture.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.custom_payload = type { [12 x i64] }

@.str = private unnamed_addr constant [3 x i8] c"%s\00", align 1, !dbg !0
@global_payload = dso_local global %struct.custom_payload zeroinitializer, align 8, !dbg !7
@__afl_area_ptr = external local_unnamed_addr global ptr
@__afl_prev_loc = external thread_local local_unnamed_addr global i32

; Function Attrs: noinline nounwind uwtable
define dso_local void @custom_receive_debug(ptr noundef %0) local_unnamed_addr #0 !dbg !28 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %4 = xor i32 %2, 9158
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1, !nosanitize !27
  %8 = add i8 %7, 1
  store i8 %8, ptr %6, align 1, !nosanitize !27
  store i32 4579, ptr @__afl_prev_loc, align 4, !nosanitize !27
  tail call void @llvm.dbg.value(metadata ptr %0, metadata !33, metadata !DIExpression()), !dbg !34
  tail call void @escape(ptr noundef %0) #9, !dbg !35
  ret void, !dbg !36
}

declare !dbg !37 void @escape(ptr noundef) local_unnamed_addr #1

; Function Attrs: noinline nounwind uwtable
define dso_local void @custom_send_debug(ptr noundef %0) local_unnamed_addr #0 !dbg !41 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %4 = xor i32 %2, 18547
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1, !nosanitize !27
  %8 = add i8 %7, 1
  store i8 %8, ptr %6, align 1, !nosanitize !27
  store i32 9273, ptr @__afl_prev_loc, align 4, !nosanitize !27
  tail call void @llvm.dbg.value(metadata ptr %0, metadata !43, metadata !DIExpression()), !dbg !44
  tail call void @escape(ptr noundef %0) #9, !dbg !45
  ret void, !dbg !46
}

; Function Attrs: noinline nounwind uwtable
define dso_local void @memory_fixture(i64 noundef %0) local_unnamed_addr #0 !dbg !47 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %4 = xor i32 %2, 23807
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1, !nosanitize !27
  %8 = add i8 %7, 1
  store i8 %8, ptr %6, align 1, !nosanitize !27
  store i32 11903, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %9 = alloca [128 x i8], align 16, !DIAssignID !64
  call void @llvm.dbg.assign(metadata i1 undef, metadata !56, metadata !DIExpression(), metadata !64, metadata ptr %9, metadata !DIExpression()), !dbg !65
  %10 = alloca [32 x i8], align 16, !DIAssignID !66
  call void @llvm.dbg.assign(metadata i1 undef, metadata !60, metadata !DIExpression(), metadata !66, metadata ptr %10, metadata !DIExpression()), !dbg !65
  tail call void @llvm.dbg.value(metadata i64 %0, metadata !53, metadata !DIExpression()), !dbg !65
  %11 = tail call noalias ptr @malloc(i64 noundef %0) #9, !dbg !67
  tail call void @new_heap_alloc_record(ptr %11, i64 %0) #10, !dbg !68
  tail call void @llvm.dbg.value(metadata ptr %11, metadata !54, metadata !DIExpression()), !dbg !65
  tail call void @escape(ptr noundef %11) #9, !dbg !69
  %12 = tail call noalias ptr @calloc(i64 noundef %0, i64 noundef 3) #9, !dbg !70
  tail call void @trace_calloc(ptr %12, i64 3, i64 %0) #10, !dbg !71
  tail call void @llvm.dbg.value(metadata ptr %12, metadata !55, metadata !DIExpression()), !dbg !65
  tail call void @escape(ptr noundef %12) #9, !dbg !72
  %13 = add i64 %0, 3, !dbg !73
  %14 = tail call ptr @realloc(ptr noundef %11, i64 noundef %13) #9, !dbg !74
  tail call void @trace_realloc(ptr %14, i64 %13, ptr %11) #10, !dbg !75
  tail call void @llvm.dbg.value(metadata ptr %14, metadata !54, metadata !DIExpression()), !dbg !65
  tail call void @escape(ptr noundef %14) #9, !dbg !76
  tail call void @free_heap_alloc_record(ptr %14, i64 0) #10, !dbg !77
  tail call void @free(ptr noundef %14) #9, !dbg !77
  tail call void @free_heap_alloc_record(ptr %12, i64 0) #10, !dbg !78
  tail call void @free(ptr noundef %12) #9, !dbg !78
  call void @new_stack_alloc_record(ptr nonnull %9, i64 128) #10, !dbg !79
  call void @llvm.lifetime.start.p0(i64 128, ptr nonnull %9) #10, !dbg !79
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %10) #10, !dbg !80
  call void @escape(ptr noundef nonnull %9) #9, !dbg !81
  call void @escape(ptr noundef nonnull %10) #9, !dbg !82
  call void @llvm.lifetime.end.p0(i64 32, ptr nonnull %10) #10, !dbg !83
  call void @free_stack_alloc_record(ptr nonnull %9, i64 128) #10, !dbg !83
  call void @llvm.lifetime.end.p0(i64 128, ptr nonnull %9) #10, !dbg !83
  ret void, !dbg !83
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #2

; Function Attrs: nounwind
declare !dbg !84 noalias ptr @malloc(i64 noundef) local_unnamed_addr #3

; Function Attrs: nounwind
declare !dbg !88 noalias ptr @calloc(i64 noundef, i64 noundef) local_unnamed_addr #3

; Function Attrs: nounwind
declare !dbg !91 ptr @realloc(ptr noundef, i64 noundef) local_unnamed_addr #3

; Function Attrs: nounwind
declare !dbg !94 void @free(ptr noundef) local_unnamed_addr #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #2

; Function Attrs: noinline nounwind uwtable
define dso_local void @io_fixture(i32 noundef %0, ptr noundef %1, ptr noundef %2, i64 noundef %3, ptr noundef %4, ptr noundef %5, ptr noundef %6) local_unnamed_addr #0 !dbg !95 {
  %8 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %9 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %10 = xor i32 %8, 22764
  %11 = sext i32 %10 to i64
  %12 = getelementptr i8, ptr %9, i64 %11
  %13 = load i8, ptr %12, align 1, !nosanitize !27
  %14 = add i8 %13, 1
  store i8 %14, ptr %12, align 1, !nosanitize !27
  store i32 11382, ptr @__afl_prev_loc, align 4, !nosanitize !27
  tail call void @llvm.dbg.value(metadata i32 %0, metadata !188, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata ptr %1, metadata !189, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata ptr %2, metadata !190, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata i64 %3, metadata !191, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata ptr %4, metadata !192, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata ptr %5, metadata !193, metadata !DIExpression()), !dbg !195
  tail call void @llvm.dbg.value(metadata ptr %6, metadata !194, metadata !DIExpression()), !dbg !195
  tail call void @trace_receive(ptr %2, i64 %3) #10, !dbg !196
  %15 = tail call i64 @recv(i32 noundef %0, ptr noundef %2, i64 noundef %3, i32 noundef 0) #9, !dbg !196
  tail call void @trace_receive(ptr %2, i64 %3) #10, !dbg !197
  %16 = tail call i64 @recvfrom(i32 noundef %0, ptr noundef %2, i64 noundef %3, i32 noundef 0, ptr noundef %5, ptr noundef %6) #9, !dbg !197
  tail call void @trace_recvmsg(ptr %4) #10, !dbg !198
  %17 = tail call i64 @recvmsg(i32 noundef %0, ptr noundef %4, i32 noundef 0) #9, !dbg !198
  tail call void @trace_send(ptr %2, i64 %3) #10, !dbg !199
  %18 = tail call i64 @send(i32 noundef %0, ptr noundef %2, i64 noundef %3, i32 noundef 0) #9, !dbg !199
  %19 = load i32, ptr %6, align 4, !dbg !200, !tbaa !201
  tail call void @trace_send(ptr %2, i64 %3) #10, !dbg !205
  %20 = tail call i64 @sendto(i32 noundef %0, ptr noundef %2, i64 noundef %3, i32 noundef 0, ptr noundef %5, i32 noundef %19) #9, !dbg !205
  tail call void @trace_sendmsg(ptr %4) #10, !dbg !206
  %21 = tail call i64 @sendmsg(i32 noundef %0, ptr noundef %4, i32 noundef 0) #9, !dbg !206
  tail call void @trace_read(i32 %0, ptr %2, i64 %3) #10, !dbg !207
  %22 = tail call i64 @read(i32 noundef %0, ptr noundef %2, i64 noundef %3) #9, !dbg !207
  tail call void @trace_write(i32 %0, ptr %2, i64 %3) #10, !dbg !208
  %23 = tail call i64 @write(i32 noundef %0, ptr noundef %2, i64 noundef %3) #9, !dbg !208
  %24 = tail call i32 (ptr, ptr, ...) @fprintf(ptr noundef %1, ptr noundef nonnull @.str, ptr noundef %2) #9, !dbg !209
  tail call void @trace_fprintf(ptr %1, ptr nonnull @.str, i32 %24) #10, !dbg !210
  tail call void @trace_fgets(ptr %1, ptr %2, i32 128) #10, !dbg !211
  %25 = tail call ptr @fgets(ptr noundef %2, i32 noundef 128, ptr noundef %1) #9, !dbg !211
  tail call void @trace_fread(ptr %1, ptr %2, i64 2, i64 %3) #10, !dbg !212
  %26 = tail call i64 @fread(ptr noundef %2, i64 noundef 2, i64 noundef %3, ptr noundef %1) #9, !dbg !212
  tail call void @trace_fwrite(ptr %1, ptr %2, i64 2, i64 %3) #10, !dbg !213
  %27 = tail call i64 @fwrite(ptr noundef %2, i64 noundef 2, i64 noundef %3, ptr noundef %1) #9, !dbg !213
  tail call void @trace_close(i32 %0) #10, !dbg !214
  %28 = tail call i32 @close(i32 noundef %0) #9, !dbg !214
  tail call void @trace_fclose(ptr %1) #10, !dbg !215
  %29 = tail call i32 @fclose(ptr noundef %1) #9, !dbg !215
  ret void, !dbg !216
}

declare !dbg !217 i64 @recv(i32 noundef, ptr noundef, i64 noundef, i32 noundef) local_unnamed_addr #1

declare !dbg !224 i64 @recvfrom(i32 noundef, ptr noundef, i64 noundef, i32 noundef, ptr noundef, ptr noundef) local_unnamed_addr #1

declare !dbg !230 i64 @recvmsg(i32 noundef, ptr noundef, i32 noundef) local_unnamed_addr #1

declare !dbg !233 i64 @send(i32 noundef, ptr noundef, i64 noundef, i32 noundef) local_unnamed_addr #1

declare !dbg !238 i64 @sendto(i32 noundef, ptr noundef, i64 noundef, i32 noundef, ptr noundef, i32 noundef) local_unnamed_addr #1

declare !dbg !243 i64 @sendmsg(i32 noundef, ptr noundef, i32 noundef) local_unnamed_addr #1

declare !dbg !248 i64 @read(i32 noundef, ptr noundef, i64 noundef) local_unnamed_addr #1

declare !dbg !252 i64 @write(i32 noundef, ptr noundef, i64 noundef) local_unnamed_addr #1

declare !dbg !255 i32 @fprintf(ptr noundef, ptr noundef, ...) local_unnamed_addr #1

declare !dbg !262 ptr @fgets(ptr noundef, i32 noundef, ptr noundef) local_unnamed_addr #1

declare !dbg !266 i64 @fread(ptr noundef, i64 noundef, i64 noundef, ptr noundef) local_unnamed_addr #1

declare !dbg !269 i64 @fwrite(ptr noundef, i64 noundef, i64 noundef, ptr noundef) local_unnamed_addr #1

declare !dbg !273 i32 @close(i32 noundef) local_unnamed_addr #1

declare !dbg !276 i32 @fclose(ptr noundef) local_unnamed_addr #1

; Function Attrs: noinline nounwind uwtable
define dso_local void @custom_fixture(ptr noundef %0) local_unnamed_addr #0 !dbg !279 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %4 = xor i32 %2, 31949
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1, !nosanitize !27
  %8 = add i8 %7, 1
  store i8 %8, ptr %6, align 1, !nosanitize !27
  store i32 15974, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %9 = alloca %struct.custom_payload, align 8, !DIAssignID !283
  call void @llvm.dbg.assign(metadata i1 undef, metadata !282, metadata !DIExpression(), metadata !283, metadata ptr %9, metadata !DIExpression()), !dbg !284
  tail call void @llvm.dbg.value(metadata ptr %0, metadata !281, metadata !DIExpression()), !dbg !284
  call void @new_stack_alloc_record(ptr nonnull %9, i64 96) #10, !dbg !285
  call void @llvm.lifetime.start.p0(i64 96, ptr nonnull %9) #10, !dbg !285
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 8 dereferenceable(96) %9, i8 0, i64 96, i1 false), !dbg !286, !DIAssignID !287
  call void @llvm.dbg.assign(metadata i8 0, metadata !282, metadata !DIExpression(), metadata !287, metadata ptr %9, metadata !DIExpression()), !dbg !284
  %10 = load i64, ptr getelementptr inbounds (%struct.custom_payload, ptr @global_payload, i64 0, i32 0, i64 1), align 8, !dbg !288, !tbaa !289
  %11 = getelementptr inbounds [12 x i64], ptr %0, i64 0, i64 3, !dbg !291
  store i64 %10, ptr %11, align 8, !dbg !292, !tbaa !289
  call void @trace_receive(ptr nonnull %9, i64 96) #10, !dbg !293
  call void @custom_receive_local(ptr noundef nonnull %9) #9, !dbg !293
  call void @trace_send(ptr nonnull %9, i64 96) #10, !dbg !294
  call void @custom_send_local(ptr noundef nonnull %9) #9, !dbg !294
  call void @trace_receive(ptr nonnull @global_payload, i64 96) #10, !dbg !295
  call void @custom_receive_global(ptr noundef nonnull @global_payload) #9, !dbg !295
  call void @trace_send(ptr nonnull @global_payload, i64 96) #10, !dbg !296
  call void @custom_send_global(ptr noundef nonnull @global_payload) #9, !dbg !296
  call void @trace_receive(ptr %0, i64 96) #10, !dbg !297
  call void @custom_receive_formal(ptr noundef %0) #9, !dbg !297
  call void @trace_send(ptr %0, i64 96) #10, !dbg !298
  call void @custom_send_formal(ptr noundef %0) #9, !dbg !298
  %12 = call ptr @external_payload() #9, !dbg !299
  call void @trace_receive(ptr %12, i64 96) #10, !dbg !300
  call void @custom_receive_external(ptr noundef %12) #9, !dbg !300
  %13 = call ptr @external_payload() #9, !dbg !301
  call void @trace_send(ptr %13, i64 96) #10, !dbg !302
  call void @custom_send_external(ptr noundef %13) #9, !dbg !302
  %14 = call ptr @external_payload() #9, !dbg !303
  call void @trace_receive(ptr %14, i64 96) #10, !dbg !304
  call void @custom_receive_debug(ptr noundef %14) #11, !dbg !304
  %15 = call ptr @external_payload() #9, !dbg !305
  call void @trace_send(ptr %15, i64 96) #10, !dbg !306
  call void @custom_send_debug(ptr noundef %15) #11, !dbg !306
  call void @trace_receive(ptr null, i64 0) #10, !dbg !307
  call void @custom_receive_nobuf() #9, !dbg !307
  call void @trace_send(ptr null, i64 0) #10, !dbg !308
  call void @custom_send_nobuf() #9, !dbg !308
  call void @free_stack_alloc_record(ptr nonnull %9, i64 96) #10, !dbg !309
  call void @llvm.lifetime.end.p0(i64 96, ptr nonnull %9) #10, !dbg !309
  ret void, !dbg !309
}

; Function Attrs: mustprogress nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #4

declare !dbg !310 void @custom_receive_local(ptr noundef) local_unnamed_addr #1

declare !dbg !311 void @custom_send_local(ptr noundef) local_unnamed_addr #1

declare !dbg !312 void @custom_receive_global(ptr noundef) local_unnamed_addr #1

declare !dbg !313 void @custom_send_global(ptr noundef) local_unnamed_addr #1

declare !dbg !314 void @custom_receive_formal(ptr noundef) local_unnamed_addr #1

declare !dbg !315 void @custom_send_formal(ptr noundef) local_unnamed_addr #1

declare !dbg !316 void @custom_receive_external(ptr noundef) local_unnamed_addr #1

declare !dbg !317 ptr @external_payload() local_unnamed_addr #1

declare !dbg !320 void @custom_send_external(ptr noundef) local_unnamed_addr #1

declare !dbg !321 void @custom_receive_nobuf() local_unnamed_addr #1

declare !dbg !324 void @custom_send_nobuf() local_unnamed_addr #1

; Function Attrs: noinline noreturn nounwind uwtable
define dso_local void @exit_fixture(i32 noundef %0) local_unnamed_addr #5 !dbg !325 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !27
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !27
  %4 = xor i32 %2, 55211
  %5 = sext i32 %4 to i64
  %6 = getelementptr i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1, !nosanitize !27
  %8 = add i8 %7, 1
  store i8 %8, ptr %6, align 1, !nosanitize !27
  store i32 27605, ptr @__afl_prev_loc, align 4, !nosanitize !27
  tail call void @llvm.dbg.value(metadata i32 %0, metadata !329, metadata !DIExpression()), !dbg !330
  tail call void @end_state_tracer() #10, !dbg !331
  tail call void @_exit(i32 noundef %0) #12, !dbg !331
  unreachable, !dbg !331
}

; Function Attrs: noreturn
declare !dbg !332 void @_exit(i32 noundef) local_unnamed_addr #6

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare void @llvm.dbg.assign(metadata, metadata, metadata, metadata, metadata, metadata) #7

declare void @new_heap_alloc_record(ptr, i64) local_unnamed_addr

declare void @trace_calloc(ptr, i64, i64) local_unnamed_addr

declare void @trace_realloc(ptr, i64, ptr) local_unnamed_addr

declare void @free_heap_alloc_record(ptr, i64) local_unnamed_addr

declare void @new_stack_alloc_record(ptr, i64) local_unnamed_addr

declare void @free_stack_alloc_record(ptr, i64) local_unnamed_addr

declare void @trace_receive(ptr, i64) local_unnamed_addr

declare void @trace_recvmsg(ptr) local_unnamed_addr

declare void @trace_send(ptr, i64) local_unnamed_addr

declare void @trace_sendmsg(ptr) local_unnamed_addr

declare void @trace_read(i32, ptr, i64) local_unnamed_addr

declare void @trace_write(i32, ptr, i64) local_unnamed_addr

declare void @trace_fprintf(ptr, ptr, i32) local_unnamed_addr

declare void @trace_fgets(ptr, ptr, i32) local_unnamed_addr

declare void @trace_fread(ptr, ptr, i64, i64) local_unnamed_addr

declare void @trace_fwrite(ptr, ptr, i64, i64) local_unnamed_addr

declare void @trace_close(i32) local_unnamed_addr

declare void @trace_fclose(ptr) local_unnamed_addr

declare void @end_state_tracer() local_unnamed_addr

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare void @llvm.dbg.value(metadata, metadata, metadata) #8

attributes #0 = { noinline nounwind uwtable "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #3 = { nounwind "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { mustprogress nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #5 = { noinline noreturn nounwind uwtable "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { noreturn "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #8 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #9 = { nobuiltin nounwind "no-builtins" }
attributes #10 = { nounwind }
attributes #11 = { nobuiltin "no-builtins" }
attributes #12 = { nobuiltin noreturn nounwind "no-builtins" }

!llvm.dbg.cu = !{!9}
!llvm.module.flags = !{!19, !20, !21, !22, !23, !24, !25}
!llvm.ident = !{!26}
!stateafl.instrumented = !{!27}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 59, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "tests/llvm18/ir_fixture.c", directory: "/home/weicresec/Desktop/stateafl", checksumkind: CSK_MD5, checksum: "3f66ab2636dec03ef0d8ee00e4974d5a")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 24, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 3)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(name: "global_payload", scope: !9, file: !2, line: 10, type: !12, isLocal: false, isDefinition: true)
!9 = distinct !DICompileUnit(language: DW_LANG_C11, file: !10, producer: "Ubuntu clang version 18.1.3 (1ubuntu1)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !11, splitDebugInlining: false, nameTableKind: None)
!10 = !DIFile(filename: "/home/weicresec/Desktop/stateafl/tests/llvm18/ir_fixture.c", directory: "/home/weicresec/Desktop/stateafl", checksumkind: CSK_MD5, checksum: "3f66ab2636dec03ef0d8ee00e4974d5a")
!11 = !{!0, !7}
!12 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "custom_payload", file: !2, line: 9, size: 768, elements: !13)
!13 = !{!14}
!14 = !DIDerivedType(tag: DW_TAG_member, name: "words", scope: !12, file: !2, line: 9, baseType: !15, size: 768)
!15 = !DICompositeType(tag: DW_TAG_array_type, baseType: !16, size: 768, elements: !17)
!16 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!17 = !{!18}
!18 = !DISubrange(count: 12)
!19 = !{i32 7, !"Dwarf Version", i32 5}
!20 = !{i32 2, !"Debug Info Version", i32 3}
!21 = !{i32 1, !"wchar_size", i32 4}
!22 = !{i32 8, !"PIC Level", i32 2}
!23 = !{i32 7, !"PIE Level", i32 2}
!24 = !{i32 7, !"uwtable", i32 2}
!25 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!26 = !{!"Ubuntu clang version 18.1.3 (1ubuntu1)"}
!27 = !{}
!28 = distinct !DISubprogram(name: "custom_receive_debug", scope: !2, file: !2, line: 25, type: !29, scopeLine: 25, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !32)
!29 = !DISubroutineType(types: !30)
!30 = !{null, !31}
!31 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !12, size: 64)
!32 = !{!33}
!33 = !DILocalVariable(name: "p", arg: 1, scope: !28, file: !2, line: 25, type: !31)
!34 = !DILocation(line: 0, scope: !28)
!35 = !DILocation(line: 26, column: 3, scope: !28)
!36 = !DILocation(line: 27, column: 1, scope: !28)
!37 = !DISubprogram(name: "escape", scope: !2, file: !2, line: 11, type: !38, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!38 = !DISubroutineType(types: !39)
!39 = !{null, !40}
!40 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!41 = distinct !DISubprogram(name: "custom_send_debug", scope: !2, file: !2, line: 28, type: !29, scopeLine: 28, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !42)
!42 = !{!43}
!43 = !DILocalVariable(name: "p", arg: 1, scope: !41, file: !2, line: 28, type: !31)
!44 = !DILocation(line: 0, scope: !41)
!45 = !DILocation(line: 29, column: 3, scope: !41)
!46 = !DILocation(line: 30, column: 1, scope: !41)
!47 = distinct !DISubprogram(name: "memory_fixture", scope: !2, file: !2, line: 32, type: !48, scopeLine: 32, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !52)
!48 = !DISubroutineType(types: !49)
!49 = !{null, !50}
!50 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !51, line: 18, baseType: !16)
!51 = !DIFile(filename: "/tmp/stateafl-deps/usr/lib/llvm-18/lib/clang/18/include/__stddef_size_t.h", directory: "", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!52 = !{!53, !54, !55, !56, !60}
!53 = !DILocalVariable(name: "length", arg: 1, scope: !47, file: !2, line: 32, type: !50)
!54 = !DILocalVariable(name: "a", scope: !47, file: !2, line: 33, type: !40)
!55 = !DILocalVariable(name: "b", scope: !47, file: !2, line: 35, type: !40)
!56 = !DILocalVariable(name: "stack_large", scope: !47, file: !2, line: 41, type: !57)
!57 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1024, elements: !58)
!58 = !{!59}
!59 = !DISubrange(count: 128)
!60 = !DILocalVariable(name: "stack_small", scope: !47, file: !2, line: 42, type: !61)
!61 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 256, elements: !62)
!62 = !{!63}
!63 = !DISubrange(count: 32)
!64 = distinct !DIAssignID()
!65 = !DILocation(line: 0, scope: !47)
!66 = distinct !DIAssignID()
!67 = !DILocation(line: 33, column: 13, scope: !47)
!68 = !DILocation(line: 33, column: 9, scope: !47)
!69 = !DILocation(line: 34, column: 3, scope: !47)
!70 = !DILocation(line: 35, column: 13, scope: !47)
!71 = !DILocation(line: 35, column: 9, scope: !47)
!72 = !DILocation(line: 36, column: 3, scope: !47)
!73 = !DILocation(line: 37, column: 25, scope: !47)
!74 = !DILocation(line: 37, column: 7, scope: !47)
!75 = !DILocation(line: 37, column: 5, scope: !47)
!76 = !DILocation(line: 38, column: 3, scope: !47)
!77 = !DILocation(line: 39, column: 3, scope: !47)
!78 = !DILocation(line: 40, column: 3, scope: !47)
!79 = !DILocation(line: 41, column: 3, scope: !47)
!80 = !DILocation(line: 42, column: 3, scope: !47)
!81 = !DILocation(line: 43, column: 3, scope: !47)
!82 = !DILocation(line: 44, column: 3, scope: !47)
!83 = !DILocation(line: 45, column: 1, scope: !47)
!84 = !DISubprogram(name: "malloc", scope: !85, file: !85, line: 672, type: !86, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!85 = !DIFile(filename: "/usr/include/stdlib.h", directory: "", checksumkind: CSK_MD5, checksum: "7fa2ecb2348a66f8b44ab9a15abd0b72")
!86 = !DISubroutineType(types: !87)
!87 = !{!40, !50}
!88 = !DISubprogram(name: "calloc", scope: !85, file: !85, line: 675, type: !89, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!89 = !DISubroutineType(types: !90)
!90 = !{!40, !50, !50}
!91 = !DISubprogram(name: "realloc", scope: !85, file: !85, line: 683, type: !92, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!92 = !DISubroutineType(types: !93)
!93 = !{!40, !40, !50}
!94 = !DISubprogram(name: "free", scope: !85, file: !85, line: 687, type: !38, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!95 = distinct !DISubprogram(name: "io_fixture", scope: !2, file: !2, line: 47, type: !96, scopeLine: 50, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !187)
!96 = !DISubroutineType(types: !97)
!97 = !{null, !98, !99, !107, !50, !156, !176, !186}
!98 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!99 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !100, size: 64)
!100 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !101, line: 7, baseType: !102)
!101 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!102 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !103, line: 49, size: 1728, elements: !104)
!103 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "7a6d4a00a37ee6b9a40cd04bd01f5d00")
!104 = !{!105, !106, !108, !109, !110, !111, !112, !113, !114, !115, !116, !117, !118, !121, !123, !124, !125, !129, !131, !133, !137, !140, !142, !145, !148, !149, !150, !151, !152}
!105 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !102, file: !103, line: 51, baseType: !98, size: 32)
!106 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !102, file: !103, line: 54, baseType: !107, size: 64, offset: 64)
!107 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!108 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !102, file: !103, line: 55, baseType: !107, size: 64, offset: 128)
!109 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !102, file: !103, line: 56, baseType: !107, size: 64, offset: 192)
!110 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !102, file: !103, line: 57, baseType: !107, size: 64, offset: 256)
!111 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !102, file: !103, line: 58, baseType: !107, size: 64, offset: 320)
!112 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !102, file: !103, line: 59, baseType: !107, size: 64, offset: 384)
!113 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !102, file: !103, line: 60, baseType: !107, size: 64, offset: 448)
!114 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !102, file: !103, line: 61, baseType: !107, size: 64, offset: 512)
!115 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !102, file: !103, line: 64, baseType: !107, size: 64, offset: 576)
!116 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !102, file: !103, line: 65, baseType: !107, size: 64, offset: 640)
!117 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !102, file: !103, line: 66, baseType: !107, size: 64, offset: 704)
!118 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !102, file: !103, line: 68, baseType: !119, size: 64, offset: 768)
!119 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !120, size: 64)
!120 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !103, line: 36, flags: DIFlagFwdDecl)
!121 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !102, file: !103, line: 70, baseType: !122, size: 64, offset: 832)
!122 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !102, size: 64)
!123 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !102, file: !103, line: 72, baseType: !98, size: 32, offset: 896)
!124 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !102, file: !103, line: 73, baseType: !98, size: 32, offset: 928)
!125 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !102, file: !103, line: 74, baseType: !126, size: 64, offset: 960)
!126 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !127, line: 152, baseType: !128)
!127 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "e1865d9fe29fe1b5ced550b7ba458f9e")
!128 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!129 = !DIDerivedType(tag: DW_TAG_member, name: "_cur_column", scope: !102, file: !103, line: 77, baseType: !130, size: 16, offset: 1024)
!130 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!131 = !DIDerivedType(tag: DW_TAG_member, name: "_vtable_offset", scope: !102, file: !103, line: 78, baseType: !132, size: 8, offset: 1040)
!132 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!133 = !DIDerivedType(tag: DW_TAG_member, name: "_shortbuf", scope: !102, file: !103, line: 79, baseType: !134, size: 8, offset: 1048)
!134 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 8, elements: !135)
!135 = !{!136}
!136 = !DISubrange(count: 1)
!137 = !DIDerivedType(tag: DW_TAG_member, name: "_lock", scope: !102, file: !103, line: 81, baseType: !138, size: 64, offset: 1088)
!138 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !139, size: 64)
!139 = !DIDerivedType(tag: DW_TAG_typedef, name: "_IO_lock_t", file: !103, line: 43, baseType: null)
!140 = !DIDerivedType(tag: DW_TAG_member, name: "_offset", scope: !102, file: !103, line: 89, baseType: !141, size: 64, offset: 1152)
!141 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !127, line: 153, baseType: !128)
!142 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !102, file: !103, line: 91, baseType: !143, size: 64, offset: 1216)
!143 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !144, size: 64)
!144 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !103, line: 37, flags: DIFlagFwdDecl)
!145 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !102, file: !103, line: 92, baseType: !146, size: 64, offset: 1280)
!146 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !147, size: 64)
!147 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !103, line: 38, flags: DIFlagFwdDecl)
!148 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !102, file: !103, line: 93, baseType: !122, size: 64, offset: 1344)
!149 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !102, file: !103, line: 94, baseType: !40, size: 64, offset: 1408)
!150 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !102, file: !103, line: 95, baseType: !50, size: 64, offset: 1472)
!151 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !102, file: !103, line: 96, baseType: !98, size: 32, offset: 1536)
!152 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !102, file: !103, line: 98, baseType: !153, size: 160, offset: 1568)
!153 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !154)
!154 = !{!155}
!155 = !DISubrange(count: 20)
!156 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !157, size: 64)
!157 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "msghdr", file: !158, line: 262, size: 448, elements: !159)
!158 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/socket.h", directory: "", checksumkind: CSK_MD5, checksum: "71a09f67661e8e55cf505c19b5ddbb85")
!159 = !{!160, !161, !165, !172, !173, !174, !175}
!160 = !DIDerivedType(tag: DW_TAG_member, name: "msg_name", scope: !157, file: !158, line: 264, baseType: !40, size: 64)
!161 = !DIDerivedType(tag: DW_TAG_member, name: "msg_namelen", scope: !157, file: !158, line: 265, baseType: !162, size: 32, offset: 64)
!162 = !DIDerivedType(tag: DW_TAG_typedef, name: "socklen_t", file: !158, line: 33, baseType: !163)
!163 = !DIDerivedType(tag: DW_TAG_typedef, name: "__socklen_t", file: !127, line: 210, baseType: !164)
!164 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!165 = !DIDerivedType(tag: DW_TAG_member, name: "msg_iov", scope: !157, file: !158, line: 267, baseType: !166, size: 64, offset: 128)
!166 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !167, size: 64)
!167 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "iovec", file: !168, line: 26, size: 128, elements: !169)
!168 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_iovec.h", directory: "", checksumkind: CSK_MD5, checksum: "a53f2c2488bb0e21e47850624c16538a")
!169 = !{!170, !171}
!170 = !DIDerivedType(tag: DW_TAG_member, name: "iov_base", scope: !167, file: !168, line: 28, baseType: !40, size: 64)
!171 = !DIDerivedType(tag: DW_TAG_member, name: "iov_len", scope: !167, file: !168, line: 29, baseType: !50, size: 64, offset: 64)
!172 = !DIDerivedType(tag: DW_TAG_member, name: "msg_iovlen", scope: !157, file: !158, line: 268, baseType: !50, size: 64, offset: 192)
!173 = !DIDerivedType(tag: DW_TAG_member, name: "msg_control", scope: !157, file: !158, line: 270, baseType: !40, size: 64, offset: 256)
!174 = !DIDerivedType(tag: DW_TAG_member, name: "msg_controllen", scope: !157, file: !158, line: 271, baseType: !50, size: 64, offset: 320)
!175 = !DIDerivedType(tag: DW_TAG_member, name: "msg_flags", scope: !157, file: !158, line: 276, baseType: !98, size: 32, offset: 384)
!176 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !177, size: 64)
!177 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "sockaddr", file: !158, line: 183, size: 128, elements: !178)
!178 = !{!179, !182}
!179 = !DIDerivedType(tag: DW_TAG_member, name: "sa_family", scope: !177, file: !158, line: 185, baseType: !180, size: 16)
!180 = !DIDerivedType(tag: DW_TAG_typedef, name: "sa_family_t", file: !181, line: 28, baseType: !130)
!181 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/sockaddr.h", directory: "", checksumkind: CSK_MD5, checksum: "5066b774f0f3cdb5cbbb5467306060db")
!182 = !DIDerivedType(tag: DW_TAG_member, name: "sa_data", scope: !177, file: !158, line: 186, baseType: !183, size: 112, offset: 16)
!183 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 112, elements: !184)
!184 = !{!185}
!185 = !DISubrange(count: 14)
!186 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !162, size: 64)
!187 = !{!188, !189, !190, !191, !192, !193, !194}
!188 = !DILocalVariable(name: "fd", arg: 1, scope: !95, file: !2, line: 47, type: !98)
!189 = !DILocalVariable(name: "stream", arg: 2, scope: !95, file: !2, line: 47, type: !99)
!190 = !DILocalVariable(name: "buffer", arg: 3, scope: !95, file: !2, line: 47, type: !107)
!191 = !DILocalVariable(name: "length", arg: 4, scope: !95, file: !2, line: 48, type: !50)
!192 = !DILocalVariable(name: "message", arg: 5, scope: !95, file: !2, line: 48, type: !156)
!193 = !DILocalVariable(name: "address", arg: 6, scope: !95, file: !2, line: 49, type: !176)
!194 = !DILocalVariable(name: "address_size", arg: 7, scope: !95, file: !2, line: 50, type: !186)
!195 = !DILocation(line: 0, scope: !95)
!196 = !DILocation(line: 51, column: 3, scope: !95)
!197 = !DILocation(line: 52, column: 3, scope: !95)
!198 = !DILocation(line: 53, column: 3, scope: !95)
!199 = !DILocation(line: 54, column: 3, scope: !95)
!200 = !DILocation(line: 55, column: 42, scope: !95)
!201 = !{!202, !202, i64 0}
!202 = !{!"int", !203, i64 0}
!203 = !{!"omnipotent char", !204, i64 0}
!204 = !{!"Simple C/C++ TBAA"}
!205 = !DILocation(line: 55, column: 3, scope: !95)
!206 = !DILocation(line: 56, column: 3, scope: !95)
!207 = !DILocation(line: 57, column: 3, scope: !95)
!208 = !DILocation(line: 58, column: 3, scope: !95)
!209 = !DILocation(line: 59, column: 3, scope: !95)
!210 = !DILocation(line: 60, column: 9, scope: !95)
!211 = !DILocation(line: 60, column: 3, scope: !95)
!212 = !DILocation(line: 61, column: 3, scope: !95)
!213 = !DILocation(line: 62, column: 3, scope: !95)
!214 = !DILocation(line: 63, column: 3, scope: !95)
!215 = !DILocation(line: 64, column: 3, scope: !95)
!216 = !DILocation(line: 65, column: 1, scope: !95)
!217 = !DISubprogram(name: "recv", scope: !218, file: !218, line: 145, type: !219, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!218 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/sys/socket.h", directory: "", checksumkind: CSK_MD5, checksum: "3beca7d0ab78f16d2e6c5759220f5d99")
!219 = !DISubroutineType(types: !220)
!220 = !{!221, !98, !40, !50, !98}
!221 = !DIDerivedType(tag: DW_TAG_typedef, name: "ssize_t", file: !222, line: 78, baseType: !223)
!222 = !DIFile(filename: "/usr/include/stdio.h", directory: "", checksumkind: CSK_MD5, checksum: "1e435c46987a169d9f9186f63a512303")
!223 = !DIDerivedType(tag: DW_TAG_typedef, name: "__ssize_t", file: !127, line: 194, baseType: !128)
!224 = !DISubprogram(name: "recvfrom", scope: !218, file: !218, line: 163, type: !225, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!225 = !DISubroutineType(types: !226)
!226 = !{!221, !98, !227, !50, !98, !228, !229}
!227 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !40)
!228 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !176)
!229 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !186)
!230 = !DISubprogram(name: "recvmsg", scope: !218, file: !218, line: 216, type: !231, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!231 = !DISubroutineType(types: !232)
!232 = !{!221, !98, !156, !98}
!233 = !DISubprogram(name: "send", scope: !218, file: !218, line: 138, type: !234, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!234 = !DISubroutineType(types: !235)
!235 = !{!221, !98, !236, !50, !98}
!236 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !237, size: 64)
!237 = !DIDerivedType(tag: DW_TAG_const_type, baseType: null)
!238 = !DISubprogram(name: "sendto", scope: !218, file: !218, line: 152, type: !239, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!239 = !DISubroutineType(types: !240)
!240 = !{!221, !98, !236, !50, !98, !241, !162}
!241 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !242, size: 64)
!242 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !177)
!243 = !DISubprogram(name: "sendmsg", scope: !218, file: !218, line: 174, type: !244, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!244 = !DISubroutineType(types: !245)
!245 = !{!221, !98, !246, !98}
!246 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !247, size: 64)
!247 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !157)
!248 = !DISubprogram(name: "read", scope: !249, file: !249, line: 371, type: !250, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!249 = !DIFile(filename: "/usr/include/unistd.h", directory: "", checksumkind: CSK_MD5, checksum: "877e832a8bb8424f9180387a13787475")
!250 = !DISubroutineType(types: !251)
!251 = !{!221, !98, !40, !50}
!252 = !DISubprogram(name: "write", scope: !249, file: !249, line: 378, type: !253, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!253 = !DISubroutineType(types: !254)
!254 = !{!221, !98, !236, !50}
!255 = !DISubprogram(name: "fprintf", scope: !222, file: !222, line: 357, type: !256, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!256 = !DISubroutineType(types: !257)
!257 = !{!98, !258, !259, null}
!258 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !99)
!259 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !260)
!260 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !261, size: 64)
!261 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!262 = !DISubprogram(name: "fgets", scope: !222, file: !222, line: 654, type: !263, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!263 = !DISubroutineType(types: !264)
!264 = !{!107, !265, !98, !258}
!265 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !107)
!266 = !DISubprogram(name: "fread", scope: !222, file: !222, line: 738, type: !267, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!267 = !DISubroutineType(types: !268)
!268 = !{!50, !227, !50, !50, !258}
!269 = !DISubprogram(name: "fwrite", scope: !222, file: !222, line: 745, type: !270, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!270 = !DISubroutineType(types: !271)
!271 = !{!50, !272, !50, !50, !258}
!272 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !236)
!273 = !DISubprogram(name: "close", scope: !249, file: !249, line: 358, type: !274, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!274 = !DISubroutineType(types: !275)
!275 = !{!98, !98}
!276 = !DISubprogram(name: "fclose", scope: !222, file: !222, line: 184, type: !277, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!277 = !DISubroutineType(types: !278)
!278 = !{!98, !99}
!279 = distinct !DISubprogram(name: "custom_fixture", scope: !2, file: !2, line: 67, type: !29, scopeLine: 67, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !280)
!280 = !{!281, !282}
!281 = !DILocalVariable(name: "parameter", arg: 1, scope: !279, file: !2, line: 67, type: !31)
!282 = !DILocalVariable(name: "local", scope: !279, file: !2, line: 68, type: !12)
!283 = distinct !DIAssignID()
!284 = !DILocation(line: 0, scope: !279)
!285 = !DILocation(line: 68, column: 3, scope: !279)
!286 = !DILocation(line: 68, column: 25, scope: !279)
!287 = distinct !DIAssignID()
!288 = !DILocation(line: 70, column: 25, scope: !279)
!289 = !{!290, !290, i64 0}
!290 = !{!"long", !203, i64 0}
!291 = !DILocation(line: 70, column: 3, scope: !279)
!292 = !DILocation(line: 70, column: 23, scope: !279)
!293 = !DILocation(line: 71, column: 3, scope: !279)
!294 = !DILocation(line: 72, column: 3, scope: !279)
!295 = !DILocation(line: 73, column: 3, scope: !279)
!296 = !DILocation(line: 74, column: 3, scope: !279)
!297 = !DILocation(line: 75, column: 3, scope: !279)
!298 = !DILocation(line: 76, column: 3, scope: !279)
!299 = !DILocation(line: 77, column: 27, scope: !279)
!300 = !DILocation(line: 77, column: 3, scope: !279)
!301 = !DILocation(line: 78, column: 24, scope: !279)
!302 = !DILocation(line: 78, column: 3, scope: !279)
!303 = !DILocation(line: 79, column: 24, scope: !279)
!304 = !DILocation(line: 79, column: 3, scope: !279)
!305 = !DILocation(line: 80, column: 21, scope: !279)
!306 = !DILocation(line: 80, column: 3, scope: !279)
!307 = !DILocation(line: 81, column: 3, scope: !279)
!308 = !DILocation(line: 82, column: 3, scope: !279)
!309 = !DILocation(line: 83, column: 1, scope: !279)
!310 = !DISubprogram(name: "custom_receive_local", scope: !2, file: !2, line: 13, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!311 = !DISubprogram(name: "custom_send_local", scope: !2, file: !2, line: 14, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!312 = !DISubprogram(name: "custom_receive_global", scope: !2, file: !2, line: 15, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!313 = !DISubprogram(name: "custom_send_global", scope: !2, file: !2, line: 16, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!314 = !DISubprogram(name: "custom_receive_formal", scope: !2, file: !2, line: 17, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!315 = !DISubprogram(name: "custom_send_formal", scope: !2, file: !2, line: 18, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!316 = !DISubprogram(name: "custom_receive_external", scope: !2, file: !2, line: 19, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!317 = !DISubprogram(name: "external_payload", scope: !2, file: !2, line: 12, type: !318, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!318 = !DISubroutineType(types: !319)
!319 = !{!31}
!320 = !DISubprogram(name: "custom_send_external", scope: !2, file: !2, line: 20, type: !29, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!321 = !DISubprogram(name: "custom_receive_nobuf", scope: !2, file: !2, line: 21, type: !322, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!322 = !DISubroutineType(types: !323)
!323 = !{null}
!324 = !DISubprogram(name: "custom_send_nobuf", scope: !2, file: !2, line: 22, type: !322, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!325 = distinct !DISubprogram(name: "exit_fixture", scope: !2, file: !2, line: 85, type: !326, scopeLine: 85, flags: DIFlagPrototyped | DIFlagNoReturn | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !9, retainedNodes: !328)
!326 = !DISubroutineType(types: !327)
!327 = !{null, !98}
!328 = !{!329}
!329 = !DILocalVariable(name: "status", arg: 1, scope: !325, file: !2, line: 85, type: !98)
!330 = !DILocation(line: 0, scope: !325)
!331 = !DILocation(line: 86, column: 3, scope: !325)
!332 = !DISubprogram(name: "_exit", scope: !249, file: !249, line: 624, type: !326, flags: DIFlagPrototyped | DIFlagNoReturn, spFlags: DISPFlagOptimized)
