# $FreeBSD: releng/12.0/sys/modules/mlx5ib/Makefile 334332 2018-05-29 14:04:57Z hselasky $
.PATH:	${SRCTOP}/sys/dev/mlx5/mlx5_ib

KMOD=mlx5ib
SRCS= \
mlx5_ib_ah.c \
mlx5_ib_cong.c \
mlx5_ib_cq.c \
mlx5_ib_doorbell.c \
mlx5_ib_gsi.c \
mlx5_ib_mad.c \
mlx5_ib_main.c \
mlx5_ib_mem.c \
mlx5_ib_mr.c \
mlx5_ib_qp.c \
mlx5_ib_srq.c \
mlx5_ib_virt.c \
device_if.h bus_if.h vnode_if.h pci_if.h \
opt_inet.h opt_inet6.h opt_ratelimit.h

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/ofed/include/uapi
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include
CFLAGS+= -DCONFIG_INFINIBAND_USER_MEM

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith ${GCC_MS_EXTENSIONS}
