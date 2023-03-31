	.file	"rv64.cpp"
	.option nopic
	.attribute arch, "rv64i2p0_m2p0_a2p0_f2p0_d2p0_c2p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 16
	.text
	.align	1
	.globl	main
	.type	main, @function
main:
.LFB1549:
	.cfi_startproc
	li	a0,2142240768
	addi	a0,a0,-1087
	ret
	.cfi_endproc
.LFE1549:
	.size	main, .-main
	.align	1
	.type	_GLOBAL__sub_I_main, @function
_GLOBAL__sub_I_main:
.LFB2042:
	.cfi_startproc
	addi	sp,sp,-16
	.cfi_def_cfa_offset 16
	sd	ra,8(sp)
	sd	s0,0(sp)
	.cfi_offset 1, -8
	.cfi_offset 8, -16
	lui	s0,%hi(_ZStL8__ioinit)
	addi	a0,s0,%lo(_ZStL8__ioinit)
	call	_ZNSt8ios_base4InitC1Ev
	lui	a2,%hi(__dso_handle)
	addi	a2,a2,%lo(__dso_handle)
	addi	a1,s0,%lo(_ZStL8__ioinit)
	lui	a0,%hi(_ZNSt8ios_base4InitD1Ev)
	addi	a0,a0,%lo(_ZNSt8ios_base4InitD1Ev)
	call	__cxa_atexit
	ld	ra,8(sp)
	.cfi_restore 1
	ld	s0,0(sp)
	.cfi_restore 8
	addi	sp,sp,16
	.cfi_def_cfa_offset 0
	jr	ra
	.cfi_endproc
.LFE2042:
	.size	_GLOBAL__sub_I_main, .-_GLOBAL__sub_I_main
	.section	.init_array,"aw"
	.align	3
	.dword	_GLOBAL__sub_I_main
	.section	.sbss,"aw",@nobits
	.align	3
	.type	_ZStL8__ioinit, @object
	.size	_ZStL8__ioinit, 1
_ZStL8__ioinit:
	.zero	1
	.hidden	__dso_handle
	.ident	"GCC: (GNU) 10.1.0"
