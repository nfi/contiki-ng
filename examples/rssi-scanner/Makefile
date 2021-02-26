CONTIKI_PROJECT = rssi-scanner
all: $(CONTIKI_PROJECT)

MAKE_MAC = MAKE_MAC_NULLMAC

CONTIKI = ../..
include $(CONTIKI)/Makefile.include

%.class: %.java
	javac $(basename $<).java

viewrssi3d: ViewRSSI3D.class
	$(MAKE) login | java ViewRSSI3D

viewrssi: ViewRSSI.class
	$(MAKE) login | java ViewRSSI
