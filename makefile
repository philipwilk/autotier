TARGET = autotier
LIBS =  -l:libfuse3.so -l:libpthread.so -l:libboost_system.a -l:libboost_filesystem.a -l:libstdc++.a -lrocksdb -lboost_serialization
CC = g++
CFLAGS = -g -std=c++11 -Wall -Wextra -I/usr/include/fuse3 -I/usr/include/fuse -D_FILE_OFFSET_BITS=64 

OBJECTS = $(patsubst %.cpp, %.o, $(wildcard src/*.cpp))
HEADERS = $(wildcard src/*.hpp)

TEST_OBJECTS = tests/view_db.o src/file.o src/tier.o src/alert.o

ifeq ($(PREFIX),)
	PREFIX := /opt/45drives/autotier
endif

.PHONY: default all clean clean-build clean-target install uninstall

default: $(TARGET)
all: default

%.o: %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean: clean-build clean-target

clean-target:
	-rm -f $(TARGET)

clean-build:
	-rm -f src/*.o

install: all
	install -m 755 autotier $(DESTDIR)$(PREFIX)/bin
	cp autotier.service /usr/lib/systemd/system/autotier.service
	systemctl daemon-reload

uninstall:
	-systemctl disable --now autotier
	-rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	-rm -f /usr/lib/systemd/system/autotier.service
	systemctl daemon-reload

tests: $(TEST_OBJECTS)
	$(CC) $(TEST_OBJECTS) -Wall $(LIBS) -o test_db
