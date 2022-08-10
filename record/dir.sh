#! /bin/bash

path_name="/home/mercury/Documents/code/qihai/qihai"
file_number=0

# function readdir() {
#   for file in $1/*
#   do
#     if [ -d ${file} ]; then
#       ((file_number++))
#       readdir "${file}"
#     else
#       ((file_number++))
#     fi
#   done
# }

# readdir "${path_name}"
# echo "Total number is ${file_number}"

record_file="a.txt"
number_file="tptfs"
max_count=0

if test -s ${record_file}
then
  cat /dev/null > ${record_file}
else
  touch ${record_file}
fi

if test -s ${number_file}
then
  cat /dev/null > ${number_file}
fi

function dir_search() {
  for file in $1/*
  do
    if [ -d ${file} ]
    then
      file_count=`ls -l ${file} |grep "^[^total]" | wc -l`
      echo "${file}: ${file_count}" >> ${record_file}

      if [ ${file_count} -ge ${max_count} ]
      then
        max_count=${file_count}
        echo ${max_count} >> ${number_file}
      fi
      # echo "${file}" >> ${record_file}
      # echo ${file}
      dir_search "${file}"
    fi
  done
}

dir_search ${path_name}
# echo ${max_count} >> ${record_file}
