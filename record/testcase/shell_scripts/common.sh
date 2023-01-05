#！ /bin/bash

file_name=$1

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
source_path_prefix="/home/mercury/Documents/code/qihai/master/qihai/contrib/netbsd-tests/fs/tptfs"

# 过滤掉源文件前缀 "t_"
temp_name=${file_name#t_*}

# 过滤掉文件后缀，并拼接
target_file_name="${source_path_prefix}/${temp_name%.sh*}_test"
target_path="root@192.168.2.109:/usr/home/wcg/workspace/riscv-world/usr/tests/sys/fs/tptfs"
echo ${target_file_name}

if [ -f ${target_file_name} ]; then
  echo "File exists!"
else
  touch ${target_file_name}
  chmod +x ${target_file_name}
  echo "#! /usr/libexec/atf-sh" > ${target_file_name}
  cat ${source_path_prefix}/${file_name} >> ${target_file_name}
  scp ${target_file_name} ${target_path}
  rm -rf ${target_file_name}
fi