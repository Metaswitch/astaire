# Top level Makefile for building astaire

# this should come first so make does the right thing by default
all: build

ROOT ?= ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

DEB_COMPONENT := astaire
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := astaire astaire-dbg

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

SUBMODULES := libmemcached sas-client cpp-common

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})
include ${MK_DIR}/astaire.mk

build: ${SUBMODULES} astaire

test: ${SUBMODULES} astaire_test

testall: $(patsubst %, %_test, ${SUBMODULES}) test

clean: $(patsubst %, %_clean, ${SUBMODULES}) astaire_clean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build

distclean: $(patsubst %, %_distclean, ${SUBMODULES}) astaire_distclean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build

include build-infra/cw-deb.mk

.PHONY: deb
deb: build deb-only

.PHONY: all build test clean distclean
