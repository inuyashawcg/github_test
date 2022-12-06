: ${TESTDIR=/home/mercury/tptfs_test}

common_cleanup() {
  if [ -e ${TESTDIR} ]; then
    rm -rf ${TESTDIR}
  fi

  echo "The test file has been cleaned."
}

common_init() {
  if [ -e ${TESTDIR} ]; then
    :
  else
    mkdir ${TESTDIR}
  fi

  echo "The test directory has been created."
}