# $FreeBSD: releng/12.0/sys/modules/mlx5/Makefile 336397 2018-07-17 10:16:32Z hselasky $
.PATH:	${SRCTOP}/sys/dev/mlx5/mlx5_core

KMOD=mlx5
SRCS= \
mlx5_alloc.c \
mlx5_cmd.c \
mlx5_cq.c \
mlx5_diagnostics.c \
mlx5_eq.c \
mlx5_fs_cmd.c \
mlx5_fs_tree.c \
mlx5_fw.c \
mlx5_fwdump.c \
mlx5_fwdump_regmaps.c \
mlx5_health.c \
mlx5_mad.c \
mlx5_main.c \
mlx5_mcg.c \
mlx5_mr.c \
mlx5_pagealloc.c \
mlx5_pd.c \
mlx5_port.c \
mlx5_qp.c \
mlx5_rl.c \
mlx5_srq.c \
mlx5_transobj.c \
mlx5_uar.c \
mlx5_vport.c \
mlx5_vsc.c \
mlx5_wq.c \
device_if.h bus_if.h vnode_if.h pci_if.h \
opt_inet.h opt_inet6.h opt_rss.h opt_ratelimit.h

CFLAGS+= -I${SRCTOP}/sys/ofed/include
CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

.include <bsd.kmod.mk>

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith ${GCC_MS_EXTENSIONS}
