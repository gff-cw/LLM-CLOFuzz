; ModuleID = '/home/weicresec/Desktop/stateafl/tests/llvm18/ir_fixture.c'
source_filename = "/home/weicresec/Desktop/stateafl/tests/llvm18/ir_fixture.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.custom_payload = type { [12 x i64] }

@.str = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@global_payload = dso_local global %struct.custom_payload zeroinitializer, align 8
@__afl_area_ptr = external global ptr
@__afl_prev_loc = external thread_local global i32

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @custom_receive_debug(ptr noundef %0) #0 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %4 = xor i32 %2, 9158
  %5 = getelementptr i8, ptr %3, i32 %4
  %6 = load i8, ptr %5, align 1, !nosanitize !6
  %7 = add i8 %6, 1
  store i8 %7, ptr %5, align 1, !nosanitize !6
  store i32 4579, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %8 = alloca ptr, align 8
  store ptr %0, ptr %8, align 8
  %9 = load ptr, ptr %8, align 8
  call void @escape(ptr noundef %9) #6
  ret void
}

declare void @escape(ptr noundef) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @custom_send_debug(ptr noundef %0) #0 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %4 = xor i32 %2, 18547
  %5 = getelementptr i8, ptr %3, i32 %4
  %6 = load i8, ptr %5, align 1, !nosanitize !6
  %7 = add i8 %6, 1
  store i8 %7, ptr %5, align 1, !nosanitize !6
  store i32 9273, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %8 = alloca ptr, align 8
  store ptr %0, ptr %8, align 8
  %9 = load ptr, ptr %8, align 8
  call void @escape(ptr noundef %9) #6
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @memory_fixture(i64 noundef %0) #0 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %4 = xor i32 %2, 23807
  %5 = getelementptr i8, ptr %3, i32 %4
  %6 = load i8, ptr %5, align 1, !nosanitize !6
  %7 = add i8 %6, 1
  store i8 %7, ptr %5, align 1, !nosanitize !6
  store i32 11903, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %8 = alloca i64, align 8
  %9 = alloca ptr, align 8
  %10 = alloca ptr, align 8
  %11 = alloca [128 x i8], align 16
  %12 = alloca [32 x i8], align 16
  store i64 %0, ptr %8, align 8
  %13 = load i64, ptr %8, align 8
  %14 = call noalias ptr @malloc(i64 noundef %13) #7
  call void @new_heap_alloc_record(ptr %14, i64 %13)
  store ptr %14, ptr %9, align 8
  %15 = load ptr, ptr %9, align 8
  call void @escape(ptr noundef %15) #6
  %16 = load i64, ptr %8, align 8
  %17 = call noalias ptr @calloc(i64 noundef %16, i64 noundef 3) #7
  call void @trace_calloc(ptr %17, i64 3, i64 %16)
  store ptr %17, ptr %10, align 8
  %18 = load ptr, ptr %10, align 8
  call void @escape(ptr noundef %18) #6
  %19 = load ptr, ptr %9, align 8
  %20 = load i64, ptr %8, align 8
  %21 = add i64 %20, 3
  %22 = call ptr @realloc(ptr noundef %19, i64 noundef %21) #7
  call void @trace_realloc(ptr %22, i64 %21, ptr %19)
  store ptr %22, ptr %9, align 8
  %23 = load ptr, ptr %9, align 8
  call void @escape(ptr noundef %23) #6
  %24 = load ptr, ptr %9, align 8
  call void @free_heap_alloc_record(ptr %24, i64 0)
  call void @free(ptr noundef %24) #7
  %25 = load ptr, ptr %10, align 8
  call void @free_heap_alloc_record(ptr %25, i64 0)
  call void @free(ptr noundef %25) #7
  %26 = getelementptr inbounds [128 x i8], ptr %11, i64 0, i64 0
  call void @escape(ptr noundef %26) #6
  %27 = getelementptr inbounds [32 x i8], ptr %12, i64 0, i64 0
  call void @escape(ptr noundef %27) #6
  ret void
}

; Function Attrs: nounwind
declare noalias ptr @malloc(i64 noundef) #2

; Function Attrs: nounwind
declare noalias ptr @calloc(i64 noundef, i64 noundef) #2

; Function Attrs: nounwind
declare ptr @realloc(ptr noundef, i64 noundef) #2

; Function Attrs: nounwind
declare void @free(ptr noundef) #2

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @io_fixture(i32 noundef %0, ptr noundef %1, ptr noundef %2, i64 noundef %3, ptr noundef %4, ptr noundef %5, ptr noundef %6) #0 {
  %8 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %9 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %10 = xor i32 %8, 22764
  %11 = getelementptr i8, ptr %9, i32 %10
  %12 = load i8, ptr %11, align 1, !nosanitize !6
  %13 = add i8 %12, 1
  store i8 %13, ptr %11, align 1, !nosanitize !6
  store i32 11382, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %14 = alloca i32, align 4
  %15 = alloca ptr, align 8
  %16 = alloca ptr, align 8
  %17 = alloca i64, align 8
  %18 = alloca ptr, align 8
  %19 = alloca ptr, align 8
  %20 = alloca ptr, align 8
  store i32 %0, ptr %14, align 4
  store ptr %1, ptr %15, align 8
  store ptr %2, ptr %16, align 8
  store i64 %3, ptr %17, align 8
  store ptr %4, ptr %18, align 8
  store ptr %5, ptr %19, align 8
  store ptr %6, ptr %20, align 8
  %21 = load i32, ptr %14, align 4
  %22 = load ptr, ptr %16, align 8
  %23 = load i64, ptr %17, align 8
  call void @trace_receive(ptr %22, i64 %23)
  %24 = call i64 @recv(i32 noundef %21, ptr noundef %22, i64 noundef %23, i32 noundef 0) #6
  %25 = load i32, ptr %14, align 4
  %26 = load ptr, ptr %16, align 8
  %27 = load i64, ptr %17, align 8
  %28 = load ptr, ptr %19, align 8
  %29 = load ptr, ptr %20, align 8
  call void @trace_receive(ptr %26, i64 %27)
  %30 = call i64 @recvfrom(i32 noundef %25, ptr noundef %26, i64 noundef %27, i32 noundef 0, ptr noundef %28, ptr noundef %29) #6
  %31 = load i32, ptr %14, align 4
  %32 = load ptr, ptr %18, align 8
  call void @trace_recvmsg(ptr %32)
  %33 = call i64 @recvmsg(i32 noundef %31, ptr noundef %32, i32 noundef 0) #6
  %34 = load i32, ptr %14, align 4
  %35 = load ptr, ptr %16, align 8
  %36 = load i64, ptr %17, align 8
  call void @trace_send(ptr %35, i64 %36)
  %37 = call i64 @send(i32 noundef %34, ptr noundef %35, i64 noundef %36, i32 noundef 0) #6
  %38 = load i32, ptr %14, align 4
  %39 = load ptr, ptr %16, align 8
  %40 = load i64, ptr %17, align 8
  %41 = load ptr, ptr %19, align 8
  %42 = load ptr, ptr %20, align 8
  %43 = load i32, ptr %42, align 4
  call void @trace_send(ptr %39, i64 %40)
  %44 = call i64 @sendto(i32 noundef %38, ptr noundef %39, i64 noundef %40, i32 noundef 0, ptr noundef %41, i32 noundef %43) #6
  %45 = load i32, ptr %14, align 4
  %46 = load ptr, ptr %18, align 8
  call void @trace_sendmsg(ptr %46)
  %47 = call i64 @sendmsg(i32 noundef %45, ptr noundef %46, i32 noundef 0) #6
  %48 = load i32, ptr %14, align 4
  %49 = load ptr, ptr %16, align 8
  %50 = load i64, ptr %17, align 8
  call void @trace_read(i32 %48, ptr %49, i64 %50)
  %51 = call i64 @read(i32 noundef %48, ptr noundef %49, i64 noundef %50) #6
  %52 = load i32, ptr %14, align 4
  %53 = load ptr, ptr %16, align 8
  %54 = load i64, ptr %17, align 8
  call void @trace_write(i32 %52, ptr %53, i64 %54)
  %55 = call i64 @write(i32 noundef %52, ptr noundef %53, i64 noundef %54) #6
  %56 = load ptr, ptr %15, align 8
  %57 = load ptr, ptr %16, align 8
  %58 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %56, ptr noundef @.str, ptr noundef %57) #6
  call void @trace_fprintf(ptr %56, ptr @.str, i32 %58)
  %59 = load ptr, ptr %16, align 8
  %60 = load ptr, ptr %15, align 8
  call void @trace_fgets(ptr %60, ptr %59, i32 128)
  %61 = call ptr @fgets(ptr noundef %59, i32 noundef 128, ptr noundef %60) #6
  %62 = load ptr, ptr %16, align 8
  %63 = load i64, ptr %17, align 8
  %64 = load ptr, ptr %15, align 8
  call void @trace_fread(ptr %64, ptr %62, i64 2, i64 %63)
  %65 = call i64 @fread(ptr noundef %62, i64 noundef 2, i64 noundef %63, ptr noundef %64) #6
  %66 = load ptr, ptr %16, align 8
  %67 = load i64, ptr %17, align 8
  %68 = load ptr, ptr %15, align 8
  call void @trace_fwrite(ptr %68, ptr %66, i64 2, i64 %67)
  %69 = call i64 @fwrite(ptr noundef %66, i64 noundef 2, i64 noundef %67, ptr noundef %68) #6
  %70 = load i32, ptr %14, align 4
  call void @trace_close(i32 %70)
  %71 = call i32 @close(i32 noundef %70) #6
  %72 = load ptr, ptr %15, align 8
  call void @trace_fclose(ptr %72)
  %73 = call i32 @fclose(ptr noundef %72) #6
  ret void
}

declare i64 @recv(i32 noundef, ptr noundef, i64 noundef, i32 noundef) #1

declare i64 @recvfrom(i32 noundef, ptr noundef, i64 noundef, i32 noundef, ptr noundef, ptr noundef) #1

declare i64 @recvmsg(i32 noundef, ptr noundef, i32 noundef) #1

declare i64 @send(i32 noundef, ptr noundef, i64 noundef, i32 noundef) #1

declare i64 @sendto(i32 noundef, ptr noundef, i64 noundef, i32 noundef, ptr noundef, i32 noundef) #1

declare i64 @sendmsg(i32 noundef, ptr noundef, i32 noundef) #1

declare i64 @read(i32 noundef, ptr noundef, i64 noundef) #1

declare i64 @write(i32 noundef, ptr noundef, i64 noundef) #1

declare i32 @fprintf(ptr noundef, ptr noundef, ...) #1

declare ptr @fgets(ptr noundef, i32 noundef, ptr noundef) #1

declare i64 @fread(ptr noundef, i64 noundef, i64 noundef, ptr noundef) #1

declare i64 @fwrite(ptr noundef, i64 noundef, i64 noundef, ptr noundef) #1

declare i32 @close(i32 noundef) #1

declare i32 @fclose(ptr noundef) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @custom_fixture(ptr noundef %0) #0 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %4 = xor i32 %2, 31949
  %5 = getelementptr i8, ptr %3, i32 %4
  %6 = load i8, ptr %5, align 1, !nosanitize !6
  %7 = add i8 %6, 1
  store i8 %7, ptr %5, align 1, !nosanitize !6
  store i32 15974, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %8 = alloca ptr, align 8
  %9 = alloca %struct.custom_payload, align 8
  store ptr %0, ptr %8, align 8
  call void @llvm.memset.p0.i64(ptr align 8 %9, i8 0, i64 96, i1 false)
  %10 = load i64, ptr getelementptr inbounds ([12 x i64], ptr @global_payload, i64 0, i64 1), align 8
  %11 = load ptr, ptr %8, align 8
  %12 = getelementptr inbounds %struct.custom_payload, ptr %11, i32 0, i32 0
  %13 = getelementptr inbounds [12 x i64], ptr %12, i64 0, i64 3
  store i64 %10, ptr %13, align 8
  call void @trace_receive(ptr %9, i64 96)
  call void @custom_receive_local(ptr noundef %9) #6
  call void @trace_send(ptr %9, i64 96)
  call void @custom_send_local(ptr noundef %9) #6
  call void @trace_receive(ptr @global_payload, i64 96)
  call void @custom_receive_global(ptr noundef @global_payload) #6
  call void @trace_send(ptr @global_payload, i64 96)
  call void @custom_send_global(ptr noundef @global_payload) #6
  %14 = load ptr, ptr %8, align 8
  call void @trace_receive(ptr %14, i64 96)
  call void @custom_receive_formal(ptr noundef %14) #6
  %15 = load ptr, ptr %8, align 8
  call void @trace_send(ptr %15, i64 96)
  call void @custom_send_formal(ptr noundef %15) #6
  %16 = call ptr @external_payload() #6
  call void @trace_receive(ptr %16, i64 96)
  call void @custom_receive_external(ptr noundef %16) #6
  %17 = call ptr @external_payload() #6
  call void @trace_send(ptr %17, i64 96)
  call void @custom_send_external(ptr noundef %17) #6
  %18 = call ptr @external_payload() #6
  call void @trace_receive(ptr %18, i64 96)
  call void @custom_receive_debug(ptr noundef %18) #6
  %19 = call ptr @external_payload() #6
  call void @trace_send(ptr %19, i64 96)
  call void @custom_send_debug(ptr noundef %19) #6
  call void @trace_receive(ptr null, i64 0)
  call void @custom_receive_nobuf() #6
  call void @trace_send(ptr null, i64 0)
  call void @custom_send_nobuf() #6
  ret void
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #3

declare void @custom_receive_local(ptr noundef) #1

declare void @custom_send_local(ptr noundef) #1

declare void @custom_receive_global(ptr noundef) #1

declare void @custom_send_global(ptr noundef) #1

declare void @custom_receive_formal(ptr noundef) #1

declare void @custom_send_formal(ptr noundef) #1

declare void @custom_receive_external(ptr noundef) #1

declare ptr @external_payload() #1

declare void @custom_send_external(ptr noundef) #1

declare void @custom_receive_nobuf() #1

declare void @custom_send_nobuf() #1

; Function Attrs: noinline noreturn nounwind optnone uwtable
define dso_local void @exit_fixture(i32 noundef %0) #4 {
  %2 = load i32, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %3 = load ptr, ptr @__afl_area_ptr, align 8, !nosanitize !6
  %4 = xor i32 %2, 55211
  %5 = getelementptr i8, ptr %3, i32 %4
  %6 = load i8, ptr %5, align 1, !nosanitize !6
  %7 = add i8 %6, 1
  store i8 %7, ptr %5, align 1, !nosanitize !6
  store i32 27605, ptr @__afl_prev_loc, align 4, !nosanitize !6
  %8 = alloca i32, align 4
  store i32 %0, ptr %8, align 4
  %9 = load i32, ptr %8, align 4
  call void @end_state_tracer()
  call void @_exit(i32 noundef %9) #8
  unreachable
}

; Function Attrs: noreturn
declare void @_exit(i32 noundef) #5

declare void @new_heap_alloc_record(ptr, i64)

declare void @trace_calloc(ptr, i64, i64)

declare void @trace_realloc(ptr, i64, ptr)

declare void @free_heap_alloc_record(ptr, i64)

declare void @trace_receive(ptr, i64)

declare void @trace_recvmsg(ptr)

declare void @trace_send(ptr, i64)

declare void @trace_sendmsg(ptr)

declare void @trace_read(i32, ptr, i64)

declare void @trace_write(i32, ptr, i64)

declare void @trace_fprintf(ptr, ptr, i32)

declare void @trace_fgets(ptr, ptr, i32)

declare void @trace_fread(ptr, ptr, i64, i64)

declare void @trace_fwrite(ptr, ptr, i64, i64)

declare void @trace_close(i32)

declare void @trace_fclose(ptr)

declare void @end_state_tracer()

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #4 = { noinline noreturn nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { noreturn "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { nobuiltin "no-builtins" }
attributes #7 = { nobuiltin nounwind "no-builtins" }
attributes #8 = { nobuiltin noreturn "no-builtins" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}
!stateafl.instrumented = !{!6}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 18.1.3 (1ubuntu1)"}
!6 = !{}
