# included mk file for astaire

HOMESTEAD_DIR := ${ROOT}/src
HOMESTEAD_TEST_DIR := ${ROOT}/tests

astaire:
	${MAKE} -C ${HOMESTEAD_DIR}

astaire_test:
	${MAKE} -C ${HOMESTEAD_DIR} test

astaire_clean:
	${MAKE} -C ${HOMESTEAD_DIR} clean

astaire_distclean: astaire_clean

.PHONY: astaire astaire_test astaire_clean astaire_distclean
