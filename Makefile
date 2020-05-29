CONTIKI_PROJECT = hello-world
all: $(CONTIKI_PROJECT)


CONTIKI = ../../..
CONTIKI_WITH_RIME = 1
CONTIKI_WITH_IP = 1
include $(CONTIKI)/Makefile.include

