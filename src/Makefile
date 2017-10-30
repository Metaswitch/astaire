TARGETS := astaire rogers

VPATH := ../modules/cpp-common/src

COMMON_SOURCES :=  alarm.cpp \
                   logger.cpp \
                   log.cpp \
                   memcached_tap_client.cpp \
                   signalhandler.cpp \
                   utils.cpp

astaire_SOURCES := ${COMMON_SOURCES} \
                   memcached_config.cpp \
                   memcachedstoreview.cpp \
                   astaire_statistics.cpp \
                   statistic.cpp \
                   zmq_lvc.cpp \
                   astaire.cpp \
                   resync_main.cpp

rogers_SOURCES :=  ${COMMON_SOURCES} \
                   base_communication_monitor.cpp \
                   communicationmonitor.cpp \
                   memcached_backend.cpp \
                   memcached_connection_pool.cpp \
                   memcached_config.cpp \
                   memcachedstoreview.cpp \
                   proxy_main.cpp \
                   proxy_server.cpp

COMMON_CPPFLAGS := -I../include \
                    -I../usr/include \
                    -I../modules/cpp-common/include \
                    -I../modules/cpp-common/test_utils

astaire_CPPFLAGS := ${COMMON_CPPFLAGS}
rogers_CPPFLAGS := ${COMMON_CPPFLAGS}

COMMON_LDFLAGS := -L../usr/lib \
                   -lpthread \
                   -lboost_filesystem \
                   -lboost_regex \
                   -lboost_system \
                   -lmemcached \
                   -lrt \
                   -lzmq

astaire_LDFLAGS := ${COMMON_LDFLAGS}

rogers_LDFLAGS := ${COMMON_LDFLAGS}

include ../build-infra/cpp.mk

# Alarm definition generation rules
ROOT := ..
MODULE_DIR := ${ROOT}/modules
include ../modules/cpp-common/makefiles/alarm-utils.mk

../usr/include/astaire_alarmdefinition.h : ../build/bin/alarm_header ../astaire.root/usr/share/clearwater/infrastructure/alarms/astaire_alarms.json
	$< -j "../astaire.root/usr/share/clearwater/infrastructure/alarms/astaire_alarms.json" -n "astaire"
	mv astaire_alarmdefinition.h $@

../usr/include/rogers_alarmdefinition.h : ../build/bin/alarm_header ../rogers.root/usr/share/clearwater/infrastructure/alarms/rogers_alarms.json
	$< -j "../rogers.root/usr/share/clearwater/infrastructure/alarms/rogers_alarms.json" -n "rogers"
	mv rogers_alarmdefinition.h $@

${astaire_OBJECT_DIR}/main.o : ../usr/include/astaire_alarmdefinition.h
${rogers_OBJECT_DIR}/proxy_main.o : ../usr/include/rogers_alarmdefinition.h
