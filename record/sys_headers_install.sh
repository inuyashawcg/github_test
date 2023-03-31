#! /bin/bash

# Copyright (C) 2020-2023 Terapines Technology (Wuhan) Co., Ltd
# All rights reserved.
#
# Install header files in sys to the specified location.
#

root_path="/home/mercury/Documents/code/qihai/master/qihai/freebsd-src"
source_prefix="${root_path}/sys"
target_prefix="${root_path}/obj/riscv/tmp/usr/include"

install_cmd_prefix="${root_path}/obj/riscv/tmp/legacy/bin/install"
install_cmd=
is_symlink=
access_permission=

# For test
test_output_file="/home/mercury/wcg_private/github_inuyasha/github_test/record/tptfs"

# Parameter @package of install command.
pkg_arg1[0]="runtime"
pkg_arg1[1]="libbsm"
pkg_arg1[2]="pf"
pkg_arg1[3]="natd"

pkg_arg2[0]="dev"

# From the print information during compilation, we can see that some
# parameters of install command are fixed.
install_arg[0]="-U"
install_arg[1]="-l s"
install_arg[2]="-C"
install_arg[3]="-T package"
install_arg[4]="-o root -g wheel"
install_arg[5]="-m"

# Distinguish installation types.
# install_types[0]="SYMT"
# install_types[1]="ASYMT"
# install_types[2]="SYMT_FORMAT_3"
# install_types[3]="SYMT_FORMAT_4"
# install_types[4]="SYMT_FORMAT_5"
# install_types[5]="SYS"
# install_types[6]="MACHINE"

# The source path and install path of some header files are symmetric:
#   source parh - ***/sys/{file}/*.h
#   install path - ***/tmp/usr/include/{file}
symmetrical_path=(
  "dev/acpica/acpi_hpet.h"
  "dev/acpica/acpiio.h"
  "dev/agp/agpreg.h"
  "bsm/audit.h"
  "bsm/audit_errno.h"
  "bsm/audit_internal.h"
  "bsm/audit_record.h"
  "bsm/audit_domain.h"
  "bsm/audit_fcntl.h"
  "bsm/audit_kevents.h"
  "bsm/audit_socket_type.h"
  "cam/cam.h"
  "cam/cam_ccb.h"
  "cam/cam_compat.h"
  "cam/cam_debug.h"
  "cam/cam_iosched.h"
  "cam/cam_periph.h"
  "cam/cam_sim.h"
  "cam/cam_xpt.h"
  "cam/cam_xpt_internal.h"
  "cam/cam_xpt_periph.h"
  "cam/cam_xpt_sim.h"
  "cam/ata/ata_all.h"
  "cam/mmc/mmc.h"
  "cam/mmc/mmc_bus.h"
  "cam/mmc/mmc_all.h"
  "cam/nvme/nvme_all.h"
  "cam/scsi/scsi_all.h"
  "cam/scsi/scsi_cd.h"
  "cam/scsi/scsi_ch.h"
  "cam/scsi/scsi_da.h"
  "cam/scsi/scsi_enc.h"
  "cam/scsi/scsi_enc_internal.h"
  "cam/scsi/scsi_iu.h"
  "cam/scsi/scsi_message.h"
  "cam/scsi/scsi_pass.h"
  "cam/scsi/scsi_pt.h"
  "cam/scsi/scsi_sa.h"
  "cam/scsi/scsi_ses.h"
  "cam/scsi/scsi_sg.h"
  "cam/scsi/scsi_targetio.h"
  "cam/scsi/smp_all.h"
  "dev/evdev/input.h"
  "dev/evdev/input-event-codes.h"
  "dev/evdev/uinput.h"
  "dev/hid/hid.h"
  "dev/hid/hidraw.h"
  "dev/pci/pcireg.h"
  "rpc/rpcsec_tls.h"
  "rpc/types.h"
  "security/audit/audit_ioctl.h"
  "security/audit/audit_private.h"
  "teken/teken.h"
  "dev/veriexec/veriexec_ioctl.h"
  "netpfil/pf/pf_mtag.h"
)

# The source path and install path of some header files are asymmetric:
#   source parh - ***/sys/{file}/*.h
#   install path - ***/tmp/usr/include/{other}/
# 
# Layout of array element:
# {source_relative_path}#{install_relative_path}
asymmetric_path=(
  "fs/cd9660/cd9660_mount.h#isofs/cd9660"
  "fs/cd9660/cd9660_node.h#isofs/cd9660"
  "fs/cd9660/cd9660_rrip.h#isofs/cd9660"
  "fs/cd9660/iso.h#isofs/cd9660"
  "fs/cd9660/iso_rrip.h#isofs/cd9660"
  "dev/hyperv/utilities/hv_snapshot.h#dev/hyperv"
  "dev/hyperv/include/hyperv.h#dev/hyperv"
  "opencrypto/cryptodev.h#crypto"
  "bsm/audit.h#security/audit"
  "netpfil/ipfilter/netinet/ip_auth.h#netinet"
  "netpfil/ipfilter/netinet/ip_compat.h#netinet"
  "netpfil/ipfilter/netinet/ip_dstlist.h#netinet"
  "netpfil/ipfilter/netinet/ip_fil.h#netinet"
  "netpfil/ipfilter/netinet/ip_frag.h#netinet"
  "netpfil/ipfilter/netinet/ip_htable.h#netinet"
  "netpfil/ipfilter/netinet/ip_lookup.h#netinet"
  "netpfil/ipfilter/netinet/ip_nat.h#netinet"
  "netpfil/ipfilter/netinet/ip_pool.h#netinet"
  "netpfil/ipfilter/netinet/ip_proxy.h#netinet"
  "netpfil/ipfilter/netinet/ip_rules.h#netinet"
  "netpfil/ipfilter/netinet/ip_scan.h#netinet"
  "netpfil/ipfilter/netinet/ip_state.h#netinet"
  "netpfil/ipfilter/netinet/ip_sync.h#netinet"
  "netpfil/ipfilter/netinet/ipf_rb.h#netinet"
  "netpfil/ipfilter/netinet/ipl.h#netinet"
  "netpfil/ipfilter/netinet/radix_ipf.h#netinet"
  "netinet/libalias/alias.h#."
  "contrib/zlib/zconf.h#."
  "contrib/zlib/zlib.h#."
  "contrib/zstd/lib/zstd.h#private/zstd"
  "contrib/ngatm/netnatm/unimsg.h#netnatm"
  "contrib/ngatm/netnatm/addr.h#netnatm"
  "contrib/ngatm/netnatm/saal/sscfu.h#netnatm/saal"
  "contrib/ngatm/netnatm/saal/sscfudef.h#netnatm/saal"
  "contrib/ngatm/netnatm/saal/sscop.h#netnatm/saal"
  "contrib/ngatm/netnatm/saal/sscopdef.h#netnatm/saal"
  "contrib/ngatm/netnatm/msg/uni_config.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/uni_hdr.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/uni_ie.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/uni_msg.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/unimsglib.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/uniprint.h#netnatm/msg"
  "contrib/ngatm/netnatm/msg/unistruct.h#netnatm/msg"
  "contrib/ngatm/netnatm/sig/uni.h#netnatm/sig"
  "contrib/ngatm/netnatm/sig/unidef.h#netnatm/sig"
  "contrib/ngatm/netnatm/sig/unisig.h#netnatm/sig"
  "ofed/include/uapi/rdma/ib_user_cm.h#rdma"
  "ofed/include/uapi/rdma/ib_user_verbs.h#rdma"
  "ofed/include/uapi/rdma/mlx4-abi.h#rdma"
  "ofed/include/uapi/rdma/mlx5-abi.h#rdma"
)

# Symmetrical path that needs to be formatted through function printf().
symmetrical_path_format_3=(
  "geom/*.h"
  "net/*.h"
  "net80211/*.h"
  "netgraph/*.h"
  "netinet/*.h"
  "netinet6/*.h"
  "netipsec/*.h"
  "netsmb/*.h"
  "nfs/*.h"
  "nfsclient/*.h"
  "nfsserver/*.h"
  "sys/*.h"
  "vm/*.h"
  "rpc/rpcsec_tls.h"
  "rpc/types.h"
)

symmetrical_path_format_4=(
  "dev/an/*.h"
  "dev/ciss/*.h"
  "dev/filemon/*.h"
  "dev/firewire/*.h"
  "dev/hwpmc/*.h"
  "dev/ic/*.h"
  "dev/iicbus/*.h"
  "dev/io/*.h"
  "dev/mfi/*.h"
  "dev/mmc/*.h"
  "dev/nvme/*.h"
  "dev/ofw/*.h"
  "dev/pbio/*.h"
  "dev/ppbus/*.h"
  "dev/pwm/*.h"
  "dev/smbus/*.h"
  "dev/speaker/*.h"
  "dev/tcp_log/*.h"
  "dev/vkbd/*.h"
  "fs/devfs/*.h"
  "fs/fdescfs/*.h"
  "fs/msdosfs/*.h"
  "fs/nfs/*.h"
  "fs/nullfs/*.h"
  "fs/procfs/*.h"
  "fs/smbfs/*.h"
  "fs/udf/*.h"
  "geom/cache/*.h"
  "geom/concat/*.h"
  "geom/eli/*.h"
  "geom/gate/*.h"
  "geom/journal/*.h"
  "geom/label/*.h"
  "geom/mirror/*.h"
  "geom/mountver/*.h"
  "geom/multipath/*.h"
  "geom/nop/*.h"
  "geom/raid/*.h"
  "geom/raid3/*.h"
  "geom/shsec/*.h"
  "geom/stripe/*.h"
  "geom/virstor/*.h"
  "net/altq/*.h"
  "net/route/*.h"
  "netgraph/atm/*.h"
  "netgraph/netflow/*.h"
  "netinet/cc/*.h"
  "netinet/netdump/*.h"
  "netinet/tcp_stacks/*.h"
  "security/mac_biba/*.h"
  "security/mac_bsdextended/*.h"
  "security/mac_lomac/*.h"
  "security/mac_mls/*.h"
  "security/mac_partition/*.h"
  "security/mac_veriexec/*.h"
  "sys/disk/*.h"
  "ufs/ffs/*.h"
  "ufs/ufs/*.h"
  "fs/cuse/*.h"
  "dev/usb/*.h"
  "dev/acpica/acpiio.h"
  "dev/acpica/acpi_hpet.h"
  "dev/agp/agpreg.h"
  "dev/evdev/input.h"
  "dev/evdev/input-event-codes.h"
  "dev/evdev/uinput.h"
  "dev/hid/hid.h"
  "dev/hid/hidraw.h"
  "dev/pci/pcireg.h"
  "dev/veriexec/veriexec_ioctl.h"
  "netpfil/pf/*.h"
)

symmetrical_path_format_5=(
  "dev/mpt/mpilib/*.h"
  "netgraph/bluetooth/include/*.h"
)

aymmetrical_path_format=(
  "../../../../sys/dev/hyperv/include/hyperv.h#dev/hyperv"
  "../../../../sys/dev/hyperv/utilities/hv_snapshot.h#dev/hyperv"
  "../../../sys/netpfil/ipfilter/netinet/*.h#netinet"
  "../../../sys/opencrypto/cryptodev.h#crypto"
  "../../../sys/riscv/include/*.h#machine"
  "../../../../sys/fs/cd9660/*.h#isofs/cd9660"
)

# sys/*.h
sys_headers=(
  "aio.h"
  "errno.h"
  "fcntl.h"
  "linker_set.h"
  "poll.h"
  "stdatomic.h"
  "stdint.h"
  "syslog.h"
  "ucontext.h"
  "_semaphore.h"
)

# machine/*.h
machine_headers=(
  "float.h"
  "floatingpoint.h"
  "stdarg.h"
)

# Install command can be divided into the following four parts:
#   - basic arguments
#   - package type
#   - source file
#   - target install path
#   - format command (optional)
#
# Layout of input parameters:
#   - symlink flag: yes or no
#   - package type
#   - access permission
#   - source path
#   - target path
function assemble_install_command() {
  local symlink_flag=$1
  local package_type=$2
  local permission=$3
  local source_path=$4
  local target_path=$5

  if [[ -z ${symlink_flag} ]] || [[ -z ${package_type} ]] || \
     [[ -z ${permission} ]] || [[ -z ${source_path} ]] || \
     [[ -z ${target_path} ]]; then
     echo "assemble_install_command: invalid parameter!"
     exit 1
  fi

  install_cmd+="${install_cmd_prefix} ${install_arg[0]}"
  if [[ ${symlink_flag} == "y" ]]; then
    install_cmd+=" ${install_arg[1]}"
  else
    install_cmd+=" ${install_arg[2]}"
  fi
  install_cmd+=" ${install_arg[3]}=${package_type}"
  install_cmd+=" ${install_arg[4]} ${install_arg[5]} ${permission}"
  install_cmd+=" ${source_path} ${target_path}"
  echo ${install_cmd} >> ${test_output_file}
}

function install_symmetric() {
  local file_prefix=
  is_symlink="n"
  access_permission="644"

  for file in ${symmetrical_path[@]}
  do
    install_cmd=
    file_prefix=${file%/*}
    # echo "${file_prefix}"
    assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_prefix}/${file}" \
                             "${target_prefix}/${file_prefix}/"
    ${install_cmd}
  done
}

function install_asymmetric() {
  local source_file=
  local target_path=
  is_symlink="n"
  access_permission="644"

  for element in ${asymmetric_path[@]}
  do
    install_cmd=
    source_file=${element%\#*}
    target_path=${element#*\#}
    assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_prefix}/${source_file}" \
                             "${target_prefix}/${target_path}/"
    ${install_cmd}
  done
}

function install_format_asymmetric() {
  local source_file=
  local target_path=
  is_symlink="y"
  access_permission="755"

  for element in ${aymmetrical_path_format[@]}
  do
    install_cmd=
    source_file=${element%\#*}
    target_path=${element#*\#}
    # echo ${source_file}
    # echo ${target_path}
    assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_file}" \
                             "${target_prefix}/${target_path}"
    cd ${root_path}
    ${install_cmd}
  done
}

function install_format_symmetric() {
  local format_type=$1
  local source_file=
  local file_prefix=
  is_symlink="y"
  access_permission="755"

  case ${format_type} in
    3)
      source_file+="\$(printf '../../../%s ' sys"
      for element in ${symmetrical_path_format_3[@]}
      do
        install_cmd=
        file_prefix=${element%\/*}
        assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_file}/${element})" \
                             "${target_prefix}/${file_prefix}"
        cd ${root_path}
        ${install_cmd}
      done
      ;;
    4)
      source_file+="\$(printf '../../../../%s ' sys"
      for element in ${symmetrical_path_format_4[@]}
      do
        install_cmd=
        file_prefix=${element%\/*}
        assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_file}/${element})" \
                             "${target_prefix}/${file_prefix}"
        cd ${root_path}
        ${install_cmd}
      done
      ;;
    5)
      source_file+="\$(printf '../../../../../%s ' sys"
      for element in ${symmetrical_path_format_5[@]}
      do
        install_cmd=
        file_prefix=${element%\/*}
        assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${source_file}/${element})" \
                             "${target_prefix}/${file_prefix}"
        cd ${root_path}
        ${install_cmd}
      done
      ;;
    *)
      ;;
  esac
}

function install_sysh() {
  local path_prefix="sys"
  is_symlink="y"
  access_permission="755"

  for file in ${sys_headers[@]}
  do
    install_cmd=
    assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${path_prefix}/${file}" \
                             "${target_prefix}/${file}"
    ${install_cmd}
  done
}

function install_machineh() {
  local path_prefix="machine"
  is_symlink="y"
  access_permission="755"

  for file in ${machine_headers[@]}
  do
    install_cmd=
    assemble_install_command "${is_symlink}" \
                             "${pkg_arg1[0]},${pkg_arg2[0]}" \
                             "${access_permission}" \
                             "${path_prefix}/${file}" \
                             "${target_prefix}/${file}"
    ${install_cmd}
  done
}

cd ${root_path}
install_symmetric
install_asymmetric
install_format_symmetric 3
install_format_symmetric 4
install_format_symmetric 5
install_format_asymmetric
install_sysh
install_machineh
