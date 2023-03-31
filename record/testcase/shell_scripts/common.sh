#！ /bin/bash

# file_name=$1

# 判断是否为库文件
# echo "string.so" |egrep "(\.o|\.so|\.a)"
# function is_library() {
#   if [ -z `echo $1 |egrep "(\.o|\.so|\.a)"` ]; then
#     # echo "This is not a library".
#     return 0
#   else
#     # echo "This is a library."
#     return 1
#   fi
# }

# 判断是否为可执行文件
# function is_exe() {
#   if [ `file $1 | awk '{print $2}'` != "ELF" ]; then
#     # echo "This is not a executable file."
#     return 0
#   else
#     # echo "This is a executable file."
#     return 1
#   fi
# }

# 过滤掉某些特定命名的文件
# if [ ! -z `echo ${file_name} |egrep "(bin|sbin|lib|usr\.bin|usr\.sbin|include|share|tests)"` ]; then
#   echo "The file is captured"
# else
#   echo "Miss"
# fi

# 从主机搬运文件到测试虚拟机
# shell 字符串截取方法
#   http://c.biancheng.net/view/1120.html
# source_path_prefix="/home/mercury/Documents/code/qihai/master/qihai/contrib/netbsd-tests/fs/tptfs"

# 过滤掉源文件前缀 "t_"
# temp_name=${file_name#t_*}

# 过滤掉文件后缀，并拼接
# target_file_name="${source_path_prefix}/${temp_name%.sh*}_test"
# target_path="root@192.168.2.109:/usr/home/wcg/workspace/riscv-world/usr/tests/sys/fs/tptfs"
# echo ${target_file_name}

# if [ -f ${target_file_name} ]; then
#   echo "File exists!"
# else
#   touch ${target_file_name}
#   chmod +x ${target_file_name}
#   echo "#! /usr/libexec/atf-sh" > ${target_file_name}
#   cat ${source_path_prefix}/${file_name} >> ${target_file_name}
#   scp ${target_file_name} ${target_path}
#   rm -rf ${target_file_name}
# fi


# 判断命令是否存在于 linux 当中
cmdline=
is_exist=

cmd_array=(
  "usr.bin/fortune/strfile"
  "usr.bin/dtc"
  "bin/cat"
  "lib/libzstd"
  "lib/libopenbsd"
  "usr.bin/rpcgen"
  "usr.bin/yacc"
  "usr.bin/lex"
  "tools/build/cross-build/fake_chflags"
  "usr.bin/gencat"
  "usr.bin/cut"
  "usr.bin/join"
  "bin/realpath"
  "usr.bin/mktemp"
  "bin/rmdir"
  "usr.bin/sed"
  "usr.bin/truncate"
  "usr.bin/tsort"
  "usr.bin/file2c"
  "usr.bin/uuencode"
  "usr.bin/uudecode"
  "usr.bin/xargs"
  "usr.bin/cap_mkdb"
  "usr.sbin/services_mkdb"
  "usr.sbin/pwd_mkdb"
  "usr.bin/mkfifo"
  "usr.bin/jot"
  "usr.sbin/zic"
  "usr.sbin/tzsetup"
  "bin/test"
  "usr.bin/bmake"
  "lib/libbz2"
  "lib/libz"
  "lib/libcrypt"
  "usr.sbin/bsnmpd/gensnmptree"
  "lib/libnv"
  "lib/libsbuf"
  "lib/liblua"
  "lib/libucl"
  "usr.sbin/crunch/crunchide"
  "usr.sbin/crunch/crunchgen"
  "usr.bin/mkimg"
  "lib/libmd"
  "usr.bin/vtfontcvt"
  "usr.bin/mandoc"
  "tools/build/bootstrap-m4"
  "usr.bin/grep"
  "lib/libnetbsd"
  "usr.bin/sort"
  "usr.bin/xinstall"
  "sbin/md5"
  "usr.bin/m4"
  "usr.sbin/nmtree"
  "libexec/flua"
  "lib/libelf"
  "lib/libdwarf"
  "usr.bin/localedef"
  "usr.bin/mkesdb"
  "usr.bin/mkcsmapper"
  "kerberos5/tools/make-roken"
  "usr.bin/awk"
  "bin/expr"
  "usr.sbin/config"
  "kerberos5/lib/libroken"
  "kerberos5/lib/libvers"
  "kerberos5/tools/asn1_compile"
  "kerberos5/tools/slc"
  "usr.bin/compile_et"
)

for element in ${cmd_array[@]}
do
  cmdline=${element##*\/}
  is_exist=`which ${cmdline}`
  if [[ -n ${is_exist} ]]; then
    echo ${cmdline}
  fi
done
