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
                   main.cpp

astaire_CPPFLAGS := -I../include \
                    -I../usr/include \
                    -I../modules/cpp-common/include \
                    -I../modules/cpp-common/test_utils

astaire_LDFLAGS := -L../usr/lib \
                   -lmemcached \
                   -lpthread \
                   -lboost_filesystem \
                   -lboost_system \
                   -lrt \
                   -lzmq

include ../build-infra/cpp.mk
