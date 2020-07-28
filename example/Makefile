# gnu99 seems to be the only standard that h2o headers compile with without errors
CFLAGS += -Wall -Wextra -std=gnu99

# if you installed h2o and its dependencies, and the h2o version you installed is the
# same as the one build by h2ow, this should be enough
#INCLUDEDIRS := -I ../include

# otherwise, all of these are needed
INCLUDEDIRS := -I ../include \
 -I ../deps/h2o/include \
 -I ../deps/h2o/deps/cloexec \
 -I ../deps/h2o/deps/brotli/c/include \
 -I ../deps/h2o/deps/golombset \
 -I ../deps/h2o/deps/hiredis \
 -I ../deps/h2o/deps/libgkc \
 -I ../deps/h2o/deps/libyrmcds \
 -I ../deps/h2o/deps/klib \
 -I ../deps/h2o/deps/neverbleed \
 -I ../deps/h2o/deps/picohttpparser \
 -I ../deps/h2o/deps/picotest \
 -I ../deps/h2o/deps/picotls/deps/cifra/src/ext \
 -I ../deps/h2o/deps/picotls/deps/cifra/src \
 -I ../deps/h2o/deps/picotls/deps/micro-ecc \
 -I ../deps/h2o/deps/picotls/include \
 -I ../deps/h2o/deps/quicly/include \
 -I ../deps/h2o/deps/yaml/include \
 -I ../deps/h2o/deps/yoml

LIBS := ../libh2ow.a -luv -lcrypto -lssl -lpthread

all: simple ssl post-parsing

simple: simple.c ../libh2ow.a
	$(CC) $(CFLAGS) $(INCLUDEDIRS) simple.c $(LIBS) -o simple

ssl: ssl.c ../libh2ow.a
	$(CC) $(CFLAGS) $(INCLUDEDIRS) ssl.c $(LIBS) -o ssl

post-parsing: post-parsing.c ../libh2ow.a
	$(CC) $(CFLAGS) $(INCLUDEDIRS) post-parsing.c $(LIBS) -o post-parsing

clean:
	$(RM) simple ssl post-parsing
