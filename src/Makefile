TARGETS := astaire

VPATH := ../modules/cpp-common/src

astaire_SOURCES := memcached_tap_client.cpp \
                   memcached_config.cpp \
                   memcachedstoreview.cpp \
                   astaire_statistics.cpp \
                   statistic.cpp \
                   zmq_lvc.cpp \
                   astaire.cpp \
                   signalhandler.cpp \
                   utils.cpp \
                   logger.cpp \
                   log.cpp \
                   alarm.cpp \
                   main.cpp \
                   health_checker.cpp \
                   exception_handler.cpp \
                   proxy_server.cpp \
                   memcached_connection_pool.cpp \
                   memcached_backend.cpp \
                   base_communication_monitor.cpp \
                   communicationmonitor.cpp

astaire_CPPFLAGS := -I../include \
                    -I../usr/include \
                    -I../modules/cpp-common/include \
                    -I../modules/cpp-common/test_utils

astaire_LDFLAGS := -L../usr/lib \
                   -lmemcached \
                   -lpthread \
                   -lboost_filesystem \
                   -lboost_regex \
                   -lboost_system \
                   -lrt \
                   -lzmq

include ../build-infra/cpp.mk

# Alarm definition generation rules
ROOT := ..
MODULE_DIR := ${ROOT}/modules
include ../modules/cpp-common/makefiles/alarm-utils.mk

../usr/include/astaire_alarmdefinition.h : ../build/bin/alarm_header ../root/usr/share/clearwater/infrastructure/alarms/astaire_alarms.json
	$< -j "../root/usr/share/clearwater/infrastructure/alarms/astaire_alarms.json" -n "astaire"
	mv astaire_alarmdefinition.h $@
${astaire_OBJECT_DIR}/main.o : ../usr/include/astaire_alarmdefinition.h
