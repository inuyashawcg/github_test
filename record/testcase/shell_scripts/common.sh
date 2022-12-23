#！ /bin/bash
# : ${TESTDIR=/home/mercury/tptfs_test}

# common_cleanup() {
#   if [ -e ${TESTDIR} ]; then
#     rm -rf ${TESTDIR}
#   fi

#   echo "The test file has been cleaned."
# }

# common_init() {
#   if [ -e ${TESTDIR} ]; then
#     :
#   else
#     mkdir ${TESTDIR}
#   fi

#   echo "The test directory has been created."
# }

# output=`bsdinstall -v`
# echo ${output}

KB=1024
MB=`expr ${KB} \* 1024`
GB=`expr ${MB} \* 1024`
TB=`expr ${GB} \* 1024`

echo "KB is ${KB}"
echo "MB is ${MB}"
echo "GB is ${GB}"
echo "TB is ${TB}"
