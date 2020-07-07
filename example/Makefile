# gnu99 seems to be the only standard that h2o headers compile with without errors
CFLAGS += -Wall -Wextra -Wpedantic -std=gnu99
INCLUDEDIRS := -I ../include
LIBS := ../libh2ow.a -luv -lcrypto -lssl -lh2o -lpthread

simple: simple.c ../libh2ow.a
	$(CC) $(CFLAGS) $(INCLUDEDIRS) simple.c $(LIBS) -o simple

clean:
	$(RM) simple
