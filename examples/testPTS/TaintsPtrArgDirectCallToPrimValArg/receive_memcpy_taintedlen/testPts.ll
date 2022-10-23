; ModuleID = 'testPts.bc'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@x = global i8 53, align 1
@y = global i8 51, align 1

; Function Attrs: nounwind uwtable
define i8* @receive(i8* %buf, i8* %temp) #0 !dbg !7 {
  %1 = alloca i8*, align 8
  %2 = alloca i8*, align 8
  store i8* %buf, i8** %1, align 8
  call void @llvm.dbg.declare(metadata i8** %1, metadata !21, metadata !22), !dbg !23
  store i8* %temp, i8** %2, align 8
  call void @llvm.dbg.declare(metadata i8** %2, metadata !24, metadata !22), !dbg !25
  %3 = load i8*, i8** %1, align 8, !dbg !26
  %4 = load i8, i8* %3, align 1, !dbg !27
  %5 = load i8*, i8** %2, align 8, !dbg !28
  store i8 %4, i8* %5, align 1, !dbg !29
  %6 = load i8*, i8** %1, align 8, !dbg !30
  store i8 65, i8* %6, align 1, !dbg !31
  %7 = load i8*, i8** %1, align 8, !dbg !32
  ret i8* %7, !dbg !33
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: nounwind uwtable
define i32 @main(i32 %argc, i8** %argv) #0 !dbg !10 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i8**, align 8
  %buf1 = alloca i8*, align 8
  %buf2 = alloca i8*, align 8
  %buf3 = alloca i8*, align 8
  %buf4 = alloca i8*, align 8
  %b = alloca i8*, align 8
  %length = alloca i32, align 4
  store i32 0, i32* %1, align 4
  store i32 %argc, i32* %2, align 4
  call void @llvm.dbg.declare(metadata i32* %2, metadata !34, metadata !22), !dbg !35
  store i8** %argv, i8*** %3, align 8
  call void @llvm.dbg.declare(metadata i8*** %3, metadata !36, metadata !22), !dbg !37
  call void @llvm.dbg.declare(metadata i8** %buf1, metadata !38, metadata !22), !dbg !39
  store i8* @x, i8** %buf1, align 8, !dbg !39
  call void @llvm.dbg.declare(metadata i8** %buf2, metadata !40, metadata !22), !dbg !41
  %4 = call noalias i8* @malloc(i64 10) #4, !dbg !42
  store i8* %4, i8** %buf2, align 8, !dbg !41
  call void @llvm.dbg.declare(metadata i8** %buf3, metadata !43, metadata !22), !dbg !44
  %5 = call noalias i8* @malloc(i64 10) #4, !dbg !45
  store i8* %5, i8** %buf3, align 8, !dbg !44
  call void @llvm.dbg.declare(metadata i8** %buf4, metadata !46, metadata !22), !dbg !47
  %6 = call noalias i8* @malloc(i64 10) #4, !dbg !48
  store i8* %6, i8** %buf4, align 8, !dbg !47
  call void @llvm.dbg.declare(metadata i8** %b, metadata !49, metadata !22), !dbg !50
  %7 = load i8*, i8** %buf1, align 8, !dbg !51
  %8 = load i8*, i8** %buf4, align 8, !dbg !52
  %9 = call i8* @receive(i8* %7, i8* %8), !dbg !53
  store i8* %9, i8** %b, align 8, !dbg !50
  call void @llvm.dbg.declare(metadata i32* %length, metadata !54, metadata !22), !dbg !55
  %10 = load i8*, i8** %buf1, align 8, !dbg !56
  %11 = load i8, i8* %10, align 1, !dbg !57
  %12 = sext i8 %11 to i32, !dbg !57
  store i32 %12, i32* %length, align 4, !dbg !55
  %13 = load i8*, i8** %buf2, align 8, !dbg !58
  %14 = load i8*, i8** %b, align 8, !dbg !59
  %15 = load i8*, i8** %buf1, align 8, !dbg !60
  %16 = load i8*, i8** %buf2, align 8, !dbg !61
  %17 = ptrtoint i8* %15 to i64, !dbg !62
  %18 = ptrtoint i8* %16 to i64, !dbg !62
  %19 = sub i64 %17, %18, !dbg !62
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %13, i8* %14, i64 %19, i32 1, i1 false), !dbg !63
  %20 = load i8*, i8** %buf3, align 8, !dbg !64
  %21 = load i8*, i8** %buf4, align 8, !dbg !65
  %22 = load i32, i32* %length, align 4, !dbg !66
  %23 = sext i32 %22 to i64, !dbg !66
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %20, i8* %21, i64 %23, i32 1, i1 false), !dbg !67
  %24 = load i8*, i8** %buf4, align 8, !dbg !68
  %25 = load i8*, i8** %b, align 8, !dbg !69
  %26 = load i8*, i8** %buf1, align 8, !dbg !70
  %27 = getelementptr inbounds i8, i8* %26, i64 0, !dbg !70
  %28 = ptrtoint i8* %27 to i64, !dbg !71
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %24, i8* %25, i64 %28, i32 1, i1 false), !dbg !72
  ret i32 0, !dbg !73
}

; Function Attrs: nounwind
declare noalias i8* @malloc(i64) #2

; Function Attrs: argmemonly nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) #3

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { argmemonly nounwind }
attributes #4 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!18, !19}
!llvm.ident = !{!20}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 3.8.0-2ubuntu4 (tags/RELEASE_380/final)", isOptimized: false, runtimeVersion: 0, emissionKind: 1, enums: !2, retainedTypes: !3, subprograms: !6, globals: !15)
!1 = !DIFile(filename: "testPts.c", directory: "/home/tuba/Documents/experiments/InfoFlowTesting/test/testPTS")
!2 = !{}
!3 = !{!4}
!4 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !5, size: 64, align: 64)
!5 = !DIBasicType(name: "char", size: 8, align: 8, encoding: DW_ATE_signed_char)
!6 = !{!7, !10}
!7 = distinct !DISubprogram(name: "receive", scope: !1, file: !1, line: 8, type: !8, isLocal: false, isDefinition: true, scopeLine: 8, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!8 = !DISubroutineType(types: !9)
!9 = !{!4, !4, !4}
!10 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 15, type: !11, isLocal: false, isDefinition: true, scopeLine: 15, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!11 = !DISubroutineType(types: !12)
!12 = !{!13, !13, !14}
!13 = !DIBasicType(name: "int", size: 32, align: 32, encoding: DW_ATE_signed)
!14 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64, align: 64)
!15 = !{!16, !17}
!16 = !DIGlobalVariable(name: "x", scope: !0, file: !1, line: 5, type: !5, isLocal: false, isDefinition: true, variable: i8* @x)
!17 = !DIGlobalVariable(name: "y", scope: !0, file: !1, line: 6, type: !5, isLocal: false, isDefinition: true, variable: i8* @y)
!18 = !{i32 2, !"Dwarf Version", i32 4}
!19 = !{i32 2, !"Debug Info Version", i32 3}
!20 = !{!"clang version 3.8.0-2ubuntu4 (tags/RELEASE_380/final)"}
!21 = !DILocalVariable(name: "buf", arg: 1, scope: !7, file: !1, line: 8, type: !4)
!22 = !DIExpression()
!23 = !DILocation(line: 8, column: 21, scope: !7)
!24 = !DILocalVariable(name: "temp", arg: 2, scope: !7, file: !1, line: 8, type: !4)
!25 = !DILocation(line: 8, column: 32, scope: !7)
!26 = !DILocation(line: 9, column: 12, scope: !7)
!27 = !DILocation(line: 9, column: 11, scope: !7)
!28 = !DILocation(line: 9, column: 4, scope: !7)
!29 = !DILocation(line: 9, column: 9, scope: !7)
!30 = !DILocation(line: 10, column: 4, scope: !7)
!31 = !DILocation(line: 10, column: 8, scope: !7)
!32 = !DILocation(line: 11, column: 10, scope: !7)
!33 = !DILocation(line: 11, column: 3, scope: !7)
!34 = !DILocalVariable(name: "argc", arg: 1, scope: !10, file: !1, line: 15, type: !13)
!35 = !DILocation(line: 15, column: 14, scope: !10)
!36 = !DILocalVariable(name: "argv", arg: 2, scope: !10, file: !1, line: 15, type: !14)
!37 = !DILocation(line: 15, column: 27, scope: !10)
!38 = !DILocalVariable(name: "buf1", scope: !10, file: !1, line: 17, type: !4)
!39 = !DILocation(line: 17, column: 11, scope: !10)
!40 = !DILocalVariable(name: "buf2", scope: !10, file: !1, line: 18, type: !4)
!41 = !DILocation(line: 18, column: 11, scope: !10)
!42 = !DILocation(line: 18, column: 25, scope: !10)
!43 = !DILocalVariable(name: "buf3", scope: !10, file: !1, line: 19, type: !4)
!44 = !DILocation(line: 19, column: 11, scope: !10)
!45 = !DILocation(line: 19, column: 25, scope: !10)
!46 = !DILocalVariable(name: "buf4", scope: !10, file: !1, line: 20, type: !4)
!47 = !DILocation(line: 20, column: 11, scope: !10)
!48 = !DILocation(line: 20, column: 25, scope: !10)
!49 = !DILocalVariable(name: "b", scope: !10, file: !1, line: 21, type: !4)
!50 = !DILocation(line: 21, column: 11, scope: !10)
!51 = !DILocation(line: 21, column: 23, scope: !10)
!52 = !DILocation(line: 21, column: 29, scope: !10)
!53 = !DILocation(line: 21, column: 15, scope: !10)
!54 = !DILocalVariable(name: "length", scope: !10, file: !1, line: 22, type: !13)
!55 = !DILocation(line: 22, column: 9, scope: !10)
!56 = !DILocation(line: 22, column: 19, scope: !10)
!57 = !DILocation(line: 22, column: 18, scope: !10)
!58 = !DILocation(line: 23, column: 12, scope: !10)
!59 = !DILocation(line: 23, column: 18, scope: !10)
!60 = !DILocation(line: 23, column: 21, scope: !10)
!61 = !DILocation(line: 23, column: 28, scope: !10)
!62 = !DILocation(line: 23, column: 26, scope: !10)
!63 = !DILocation(line: 23, column: 5, scope: !10)
!64 = !DILocation(line: 24, column: 12, scope: !10)
!65 = !DILocation(line: 24, column: 18, scope: !10)
!66 = !DILocation(line: 24, column: 24, scope: !10)
!67 = !DILocation(line: 24, column: 5, scope: !10)
!68 = !DILocation(line: 25, column: 12, scope: !10)
!69 = !DILocation(line: 25, column: 18, scope: !10)
!70 = !DILocation(line: 25, column: 22, scope: !10)
!71 = !DILocation(line: 25, column: 21, scope: !10)
!72 = !DILocation(line: 25, column: 5, scope: !10)
!73 = !DILocation(line: 26, column: 5, scope: !10)
