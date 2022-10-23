; ModuleID = 'cveExample3.bc'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.ssl_ctx = type { i8*, i32 }

; Function Attrs: nounwind uwtable
define void @TLS_Init(%struct.ssl_ctx** %p) #0 !dbg !13 {
  %1 = alloca %struct.ssl_ctx**, align 8
  store %struct.ssl_ctx** %p, %struct.ssl_ctx*** %1, align 8
  call void @llvm.dbg.declare(metadata %struct.ssl_ctx*** %1, metadata !32, metadata !33), !dbg !34
  %2 = call noalias i8* @malloc(i64 16) #4, !dbg !35
  %3 = bitcast i8* %2 to %struct.ssl_ctx*, !dbg !36
  %4 = load %struct.ssl_ctx**, %struct.ssl_ctx*** %1, align 8, !dbg !37
  store %struct.ssl_ctx* %3, %struct.ssl_ctx** %4, align 8, !dbg !38
  ret void, !dbg !39
}

; Function Attrs: nounwind readnone
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: nounwind
declare noalias i8* @malloc(i64) #2

; Function Attrs: nounwind uwtable
define i32 @mbedtls_ssl_handshake(%struct.ssl_ctx* %p) #0 !dbg !17 {
  %1 = alloca i32, align 4
  %2 = alloca %struct.ssl_ctx*, align 8
  store %struct.ssl_ctx* %p, %struct.ssl_ctx** %2, align 8
  call void @llvm.dbg.declare(metadata %struct.ssl_ctx** %2, metadata !40, metadata !33), !dbg !41
  %3 = load %struct.ssl_ctx*, %struct.ssl_ctx** %2, align 8, !dbg !42
  %4 = getelementptr inbounds %struct.ssl_ctx, %struct.ssl_ctx* %3, i32 0, i32 1, !dbg !44
  %5 = load i32, i32* %4, align 8, !dbg !44
  %6 = icmp sgt i32 %5, 0, !dbg !45
  br i1 %6, label %7, label %8, !dbg !46

; <label>:7                                       ; preds = %0
  store i32 0, i32* %1, align 4, !dbg !47
  br label %21, !dbg !47

; <label>:8                                       ; preds = %0
  %9 = load %struct.ssl_ctx*, %struct.ssl_ctx** %2, align 8, !dbg !48
  %10 = getelementptr inbounds %struct.ssl_ctx, %struct.ssl_ctx* %9, i32 0, i32 1, !dbg !50
  %11 = load i32, i32* %10, align 8, !dbg !50
  %12 = icmp eq i32 %11, -1, !dbg !51
  br i1 %12, label %13, label %14, !dbg !52

; <label>:13                                      ; preds = %8
  store i32 2, i32* %1, align 4, !dbg !53
  br label %21, !dbg !53

; <label>:14                                      ; preds = %8
  %15 = load %struct.ssl_ctx*, %struct.ssl_ctx** %2, align 8, !dbg !54
  %16 = getelementptr inbounds %struct.ssl_ctx, %struct.ssl_ctx* %15, i32 0, i32 1, !dbg !56
  %17 = load i32, i32* %16, align 8, !dbg !56
  %18 = icmp eq i32 %17, -2, !dbg !57
  br i1 %18, label %19, label %20, !dbg !58

; <label>:19                                      ; preds = %14
  store i32 -1, i32* %1, align 4, !dbg !59
  br label %21, !dbg !59

; <label>:20                                      ; preds = %14
  store i32 1, i32* %1, align 4, !dbg !60
  br label %21, !dbg !60

; <label>:21                                      ; preds = %20, %19, %13, %7
  %22 = load i32, i32* %1, align 4, !dbg !61
  ret i32 %22, !dbg !61
}

; Function Attrs: nounwind uwtable
define i32 @mbedtls_ssl_read(%struct.ssl_ctx* %p) #0 !dbg !20 {
  %1 = alloca %struct.ssl_ctx*, align 8
  store %struct.ssl_ctx* %p, %struct.ssl_ctx** %1, align 8
  call void @llvm.dbg.declare(metadata %struct.ssl_ctx** %1, metadata !62, metadata !33), !dbg !63
  %2 = load %struct.ssl_ctx*, %struct.ssl_ctx** %1, align 8, !dbg !64
  %3 = getelementptr inbounds %struct.ssl_ctx, %struct.ssl_ctx* %2, i32 0, i32 1, !dbg !65
  %4 = load i32, i32* %3, align 8, !dbg !65
  ret i32 %4, !dbg !66
}

; Function Attrs: nounwind uwtable
define i32 @TLS_Connect(%struct.ssl_ctx* %p) #0 !dbg !21 {
  %1 = alloca %struct.ssl_ctx*, align 8
  store %struct.ssl_ctx* %p, %struct.ssl_ctx** %1, align 8
  call void @llvm.dbg.declare(metadata %struct.ssl_ctx** %1, metadata !67, metadata !33), !dbg !68
  %2 = load %struct.ssl_ctx*, %struct.ssl_ctx** %1, align 8, !dbg !69
  %3 = call i32 @mbedtls_ssl_handshake(%struct.ssl_ctx* %2), !dbg !70
  ret i32 %3, !dbg !71
}

; Function Attrs: nounwind uwtable
define i32 @SOCKETS_Connect() #0 !dbg !22 {
  %1 = alloca i32, align 4
  %sp = alloca %struct.ssl_ctx*, align 8
  %res = alloca i32, align 4
  %lengthc = alloca i32, align 4
  %lengthc1 = alloca i32, align 4
  %lengthc2 = alloca i32, align 4
  %lengthcor = alloca i32, align 4
  call void @llvm.dbg.declare(metadata %struct.ssl_ctx** %sp, metadata !72, metadata !33), !dbg !73
  %2 = call i32 (%struct.ssl_ctx**, ...) bitcast (i32 (...)* @TLS_init to i32 (%struct.ssl_ctx**, ...)*)(%struct.ssl_ctx** %sp), !dbg !74
  call void @llvm.dbg.declare(metadata i32* %res, metadata !75, metadata !33), !dbg !76
  %3 = load %struct.ssl_ctx*, %struct.ssl_ctx** %sp, align 8, !dbg !77
  %4 = call i32 @TLS_Connect(%struct.ssl_ctx* %3), !dbg !78
  store i32 %4, i32* %res, align 4, !dbg !76
  %5 = load i32, i32* %res, align 4, !dbg !79
  %6 = icmp eq i32 %5, 0, !dbg !81
  br i1 %6, label %7, label %10, !dbg !82

; <label>:7                                       ; preds = %0
  call void @llvm.dbg.declare(metadata i32* %lengthc, metadata !83, metadata !33), !dbg !85
  %8 = load %struct.ssl_ctx*, %struct.ssl_ctx** %sp, align 8, !dbg !86
  %9 = call i32 @mbedtls_ssl_read(%struct.ssl_ctx* %8), !dbg !87
  store i32 %9, i32* %lengthc, align 4, !dbg !85
  store i32 0, i32* %1, align 4, !dbg !88
  br label %25, !dbg !88

; <label>:10                                      ; preds = %0
  %11 = load i32, i32* %res, align 4, !dbg !89
  %12 = icmp eq i32 %11, 2, !dbg !91
  br i1 %12, label %13, label %16, !dbg !92

; <label>:13                                      ; preds = %10
  call void @llvm.dbg.declare(metadata i32* %lengthc1, metadata !93, metadata !33), !dbg !95
  %14 = load %struct.ssl_ctx*, %struct.ssl_ctx** %sp, align 8, !dbg !96
  %15 = call i32 @mbedtls_ssl_read(%struct.ssl_ctx* %14), !dbg !97
  store i32 %15, i32* %lengthc1, align 4, !dbg !95
  store i32 0, i32* %1, align 4, !dbg !98
  br label %25, !dbg !98

; <label>:16                                      ; preds = %10
  %17 = load i32, i32* %res, align 4, !dbg !99
  %18 = icmp eq i32 %17, -1, !dbg !101
  br i1 %18, label %19, label %22, !dbg !102

; <label>:19                                      ; preds = %16
  call void @llvm.dbg.declare(metadata i32* %lengthc2, metadata !103, metadata !33), !dbg !105
  %20 = load %struct.ssl_ctx*, %struct.ssl_ctx** %sp, align 8, !dbg !106
  %21 = call i32 @mbedtls_ssl_read(%struct.ssl_ctx* %20), !dbg !107
  store i32 %21, i32* %lengthc2, align 4, !dbg !105
  store i32 0, i32* %1, align 4, !dbg !108
  br label %25, !dbg !108

; <label>:22                                      ; preds = %16
  call void @llvm.dbg.declare(metadata i32* %lengthcor, metadata !109, metadata !33), !dbg !111
  %23 = load %struct.ssl_ctx*, %struct.ssl_ctx** %sp, align 8, !dbg !112
  %24 = call i32 @mbedtls_ssl_read(%struct.ssl_ctx* %23), !dbg !113
  store i32 %24, i32* %lengthcor, align 4, !dbg !111
  store i32 1, i32* %1, align 4, !dbg !114
  br label %25, !dbg !114

; <label>:25                                      ; preds = %22, %19, %13, %7
  %26 = load i32, i32* %1, align 4, !dbg !115
  ret i32 %26, !dbg !115
}

declare i32 @TLS_init(...) #3

; Function Attrs: nounwind uwtable
define i32 @main(i32 %argc, i8** %argv) #0 !dbg !25 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i8**, align 8
  store i32 0, i32* %1, align 4
  store i32 %argc, i32* %2, align 4
  call void @llvm.dbg.declare(metadata i32* %2, metadata !116, metadata !33), !dbg !117
  store i8** %argv, i8*** %3, align 8
  call void @llvm.dbg.declare(metadata i8*** %3, metadata !118, metadata !33), !dbg !119
  %4 = call i32 @SOCKETS_Connect(), !dbg !120
  ret i32 0, !dbg !121
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!29, !30}
!llvm.ident = !{!31}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 3.8.0-2ubuntu4 (tags/RELEASE_380/final)", isOptimized: false, runtimeVersion: 0, emissionKind: 1, enums: !2, retainedTypes: !3, subprograms: !12)
!1 = !DIFile(filename: "cveExample3.c", directory: "/home/tuba/Documents/experiments/InfoFlowTesting/test/testcve3")
!2 = !{}
!3 = !{!4}
!4 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !5, size: 64, align: 64)
!5 = !DICompositeType(tag: DW_TAG_structure_type, name: "ssl_ctx", file: !1, line: 5, size: 128, align: 64, elements: !6)
!6 = !{!7, !10}
!7 = !DIDerivedType(tag: DW_TAG_member, name: "data", scope: !5, file: !1, line: 6, baseType: !8, size: 64, align: 64)
!8 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !9, size: 64, align: 64)
!9 = !DIBasicType(name: "char", size: 8, align: 8, encoding: DW_ATE_signed_char)
!10 = !DIDerivedType(tag: DW_TAG_member, name: "size", scope: !5, file: !1, line: 7, baseType: !11, size: 32, align: 32, offset: 64)
!11 = !DIBasicType(name: "int", size: 32, align: 32, encoding: DW_ATE_signed)
!12 = !{!13, !17, !20, !21, !22, !25}
!13 = distinct !DISubprogram(name: "TLS_Init", scope: !1, file: !1, line: 11, type: !14, isLocal: false, isDefinition: true, scopeLine: 11, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!14 = !DISubroutineType(types: !15)
!15 = !{null, !16}
!16 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64, align: 64)
!17 = distinct !DISubprogram(name: "mbedtls_ssl_handshake", scope: !1, file: !1, line: 16, type: !18, isLocal: false, isDefinition: true, scopeLine: 16, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!18 = !DISubroutineType(types: !19)
!19 = !{!11, !4}
!20 = distinct !DISubprogram(name: "mbedtls_ssl_read", scope: !1, file: !1, line: 26, type: !18, isLocal: false, isDefinition: true, scopeLine: 26, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!21 = distinct !DISubprogram(name: "TLS_Connect", scope: !1, file: !1, line: 30, type: !18, isLocal: false, isDefinition: true, scopeLine: 30, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!22 = distinct !DISubprogram(name: "SOCKETS_Connect", scope: !1, file: !1, line: 34, type: !23, isLocal: false, isDefinition: true, scopeLine: 34, isOptimized: false, variables: !2)
!23 = !DISubroutineType(types: !24)
!24 = !{!11}
!25 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 57, type: !26, isLocal: false, isDefinition: true, scopeLine: 57, flags: DIFlagPrototyped, isOptimized: false, variables: !2)
!26 = !DISubroutineType(types: !27)
!27 = !{!11, !11, !28}
!28 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 64, align: 64)
!29 = !{i32 2, !"Dwarf Version", i32 4}
!30 = !{i32 2, !"Debug Info Version", i32 3}
!31 = !{!"clang version 3.8.0-2ubuntu4 (tags/RELEASE_380/final)"}
!32 = !DILocalVariable(name: "p", arg: 1, scope: !13, file: !1, line: 11, type: !16)
!33 = !DIExpression()
!34 = !DILocation(line: 11, column: 32, scope: !13)
!35 = !DILocation(line: 12, column: 26, scope: !13)
!36 = !DILocation(line: 12, column: 8, scope: !13)
!37 = !DILocation(line: 12, column: 4, scope: !13)
!38 = !DILocation(line: 12, column: 6, scope: !13)
!39 = !DILocation(line: 13, column: 1, scope: !13)
!40 = !DILocalVariable(name: "p", arg: 1, scope: !17, file: !1, line: 16, type: !4)
!41 = !DILocation(line: 16, column: 43, scope: !17)
!42 = !DILocation(line: 17, column: 7, scope: !43)
!43 = distinct !DILexicalBlock(scope: !17, file: !1, line: 17, column: 7)
!44 = !DILocation(line: 17, column: 10, scope: !43)
!45 = !DILocation(line: 17, column: 15, scope: !43)
!46 = !DILocation(line: 17, column: 7, scope: !17)
!47 = !DILocation(line: 18, column: 6, scope: !43)
!48 = !DILocation(line: 19, column: 12, scope: !49)
!49 = distinct !DILexicalBlock(scope: !43, file: !1, line: 19, column: 12)
!50 = !DILocation(line: 19, column: 15, scope: !49)
!51 = !DILocation(line: 19, column: 20, scope: !49)
!52 = !DILocation(line: 19, column: 12, scope: !43)
!53 = !DILocation(line: 20, column: 6, scope: !49)
!54 = !DILocation(line: 21, column: 12, scope: !55)
!55 = distinct !DILexicalBlock(scope: !49, file: !1, line: 21, column: 12)
!56 = !DILocation(line: 21, column: 15, scope: !55)
!57 = !DILocation(line: 21, column: 20, scope: !55)
!58 = !DILocation(line: 21, column: 12, scope: !49)
!59 = !DILocation(line: 22, column: 6, scope: !55)
!60 = !DILocation(line: 23, column: 8, scope: !55)
!61 = !DILocation(line: 24, column: 1, scope: !17)
!62 = !DILocalVariable(name: "p", arg: 1, scope: !20, file: !1, line: 26, type: !4)
!63 = !DILocation(line: 26, column: 38, scope: !20)
!64 = !DILocation(line: 27, column: 11, scope: !20)
!65 = !DILocation(line: 27, column: 14, scope: !20)
!66 = !DILocation(line: 27, column: 4, scope: !20)
!67 = !DILocalVariable(name: "p", arg: 1, scope: !21, file: !1, line: 30, type: !4)
!68 = !DILocation(line: 30, column: 33, scope: !21)
!69 = !DILocation(line: 31, column: 33, scope: !21)
!70 = !DILocation(line: 31, column: 11, scope: !21)
!71 = !DILocation(line: 31, column: 4, scope: !21)
!72 = !DILocalVariable(name: "sp", scope: !22, file: !1, line: 35, type: !4)
!73 = !DILocation(line: 35, column: 19, scope: !22)
!74 = !DILocation(line: 36, column: 3, scope: !22)
!75 = !DILocalVariable(name: "res", scope: !22, file: !1, line: 37, type: !11)
!76 = !DILocation(line: 37, column: 7, scope: !22)
!77 = !DILocation(line: 37, column: 25, scope: !22)
!78 = !DILocation(line: 37, column: 13, scope: !22)
!79 = !DILocation(line: 38, column: 7, scope: !80)
!80 = distinct !DILexicalBlock(scope: !22, file: !1, line: 38, column: 7)
!81 = !DILocation(line: 38, column: 11, scope: !80)
!82 = !DILocation(line: 38, column: 7, scope: !22)
!83 = !DILocalVariable(name: "lengthc", scope: !84, file: !1, line: 39, type: !11)
!84 = distinct !DILexicalBlock(scope: !80, file: !1, line: 38, column: 17)
!85 = !DILocation(line: 39, column: 10, scope: !84)
!86 = !DILocation(line: 39, column: 37, scope: !84)
!87 = !DILocation(line: 39, column: 20, scope: !84)
!88 = !DILocation(line: 40, column: 6, scope: !84)
!89 = !DILocation(line: 42, column: 12, scope: !90)
!90 = distinct !DILexicalBlock(scope: !80, file: !1, line: 42, column: 12)
!91 = !DILocation(line: 42, column: 16, scope: !90)
!92 = !DILocation(line: 42, column: 12, scope: !80)
!93 = !DILocalVariable(name: "lengthc", scope: !94, file: !1, line: 43, type: !11)
!94 = distinct !DILexicalBlock(scope: !90, file: !1, line: 42, column: 22)
!95 = !DILocation(line: 43, column: 10, scope: !94)
!96 = !DILocation(line: 43, column: 37, scope: !94)
!97 = !DILocation(line: 43, column: 20, scope: !94)
!98 = !DILocation(line: 44, column: 6, scope: !94)
!99 = !DILocation(line: 46, column: 12, scope: !100)
!100 = distinct !DILexicalBlock(scope: !90, file: !1, line: 46, column: 12)
!101 = !DILocation(line: 46, column: 16, scope: !100)
!102 = !DILocation(line: 46, column: 12, scope: !90)
!103 = !DILocalVariable(name: "lengthc", scope: !104, file: !1, line: 47, type: !11)
!104 = distinct !DILexicalBlock(scope: !100, file: !1, line: 46, column: 23)
!105 = !DILocation(line: 47, column: 10, scope: !104)
!106 = !DILocation(line: 47, column: 37, scope: !104)
!107 = !DILocation(line: 47, column: 20, scope: !104)
!108 = !DILocation(line: 48, column: 6, scope: !104)
!109 = !DILocalVariable(name: "lengthcor", scope: !110, file: !1, line: 51, type: !11)
!110 = distinct !DILexicalBlock(scope: !100, file: !1, line: 50, column: 9)
!111 = !DILocation(line: 51, column: 10, scope: !110)
!112 = !DILocation(line: 51, column: 39, scope: !110)
!113 = !DILocation(line: 51, column: 22, scope: !110)
!114 = !DILocation(line: 52, column: 6, scope: !110)
!115 = !DILocation(line: 54, column: 1, scope: !22)
!116 = !DILocalVariable(name: "argc", arg: 1, scope: !25, file: !1, line: 57, type: !11)
!117 = !DILocation(line: 57, column: 14, scope: !25)
!118 = !DILocalVariable(name: "argv", arg: 2, scope: !25, file: !1, line: 57, type: !28)
!119 = !DILocation(line: 57, column: 27, scope: !25)
!120 = !DILocation(line: 58, column: 5, scope: !25)
!121 = !DILocation(line: 59, column: 5, scope: !25)
