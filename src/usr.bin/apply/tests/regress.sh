# $FreeBSD: releng/12.0/usr.bin/apply/tests/regress.sh 263227 2014-03-16 08:04:06Z jmmv $

echo 1..2

REGRESSION_START($1)

REGRESSION_TEST(`00', `apply "echo %1 %1 %1 %1" $(cat ${SRCDIR}/regress.00.in)')
REGRESSION_TEST(`01', `sh ${SRCDIR}/regress.01.sh')

REGRESSION_END()