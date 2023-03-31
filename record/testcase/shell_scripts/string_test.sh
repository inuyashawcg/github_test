#! /bin/bash
# 参考：
# https://www.cnblogs.com/liangyou666/p/10877630.html

# filename="/home/mercury/Documents/code/qihai/work/buildrootfs/rootfs.img"
# size=`ls -l ${filename} | awk '{ print $5 }'`
# size=$1
# unit=$2
# if [[ ${size} = "" ]]; then
#   echo "Error: invalid size!"
#   echo "Please check the input parameters."
#   exit 1
# elif [ `echo ${size} |sed 's/\.//g' | sed 's/-//g' | grep [^0-9]` ]; then
#   echo "Error: size is not a number!"
#   exit 1
# elif [ `echo ${size} | grep [-]` ]; then
#   echo "Error: size may be a negative number!"
#   exit 1
# elif [ `echo ${size} | grep [.]` ]; then
#   echo "Warning: Size should preferably be an integer!"
#   exit 1
# elif [ ${size} = "0" ]; then
#     echo "Error: rootfs image size is 0!"
#     exit 1
# fi

# case $unit in
#   [Mm])
#     echo "m"
#     ;;
#   *)
#     echo "end"
#     ;;
# esac

source_file="\$(printf '../../../%s '"

echo ${source_file}