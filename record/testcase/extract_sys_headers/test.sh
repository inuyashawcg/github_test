#! /bin/bash
#
# 从 buildworld stage-4.1 中提取需要处理的头文件，并将它们安装到指定的路径下.
# sed 文件写入:
#   https://juejin.cn/post/7050759567453618206
# 
# sed 转义字符:
#   https://www.jianshu.com/p/4346e4a04ec2

source_file="source.txt"
temp_file="all_install_entry.txt"
lib_header_file="lib_header.txt"
sys_header_file="sys_header.txt"

sys_path_regex="freebsd-src\/sys"
sys_header_regex="${sys_path_regex}\/[^\t]*\.h"
lib_header_regex="freebsd-src\/lib"

lib_path_regex=""

# 提取 sys 需要处理的头文件
# sed -n "/install/p " ${source_file} > ${lib_header_file}
# sed -n "/${sys_path_regex}/w ${sys_header_file}" ${lib_header_file}
# sed -n "/${sys_path_regex}/p" ${lib_header_file} | grep -Eo ${sys_header_regex} > ${temp_file}

# 创建目录项
# sed -n "/Creating.*objdir/w ${temp_file}" ${source_file}

# 提取 lib 需要处理的头文件
# sed -n -e "/^install.*${sys_path_regex}.*/w ${lib_header_file}" ${source_file}
sed -n "/${lib_header_regex}/w ${lib_header_file}" ${temp_file}