/*-
 * Test 0010:	BPF_LD+BPF_H+BPF_IND
 *
 * $FreeBSD: releng/12.0/tools/regression/bpf/bpf_filter/tests/test0010.h 307708 2016-10-21 06:56:30Z jkim $
 */

/* BPF program */
static struct bpf_insn	pc[] = {
	BPF_STMT(BPF_LDX+BPF_IMM, 1),
	BPF_STMT(BPF_LD+BPF_H+BPF_IND, 1),
	BPF_STMT(BPF_RET+BPF_A, 0),
};

/* Packet */
static u_char	pkt[] = {
	0x01, 0x23, 0x45, 0x67,
};

/* Packet length seen on wire */
static u_int	wirelen =	sizeof(pkt);

/* Packet length passed on buffer */
static u_int	buflen =	sizeof(pkt);

/* Invalid instruction */
static int	invalid =	0;

/* Expected return value */
static u_int	expect =	0x4567;

/* Expected signal */
static int	expect_signal =	0;