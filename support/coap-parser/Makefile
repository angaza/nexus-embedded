# Nexus CoAP Parser CLI Tool
# (c) 2021 Angaza, Inc.
# This file is released under the MIT license
# The above copyright notice and license shall be included in all copies
# or substantial portions of the Software.

# default goal for when make is run by itself (`make` === `make clean all`)
cleanfirst: clean all

INCLUDES:= -I../../nexus -I../../nexus/include -I../../nexus/utils -I../../nexus/oc/ -I../../nexus/oc/api -I../../nexus/oc/include -I../../nexus/oc/messaging/coap -I../../nexus/oc/port -I../../nexus/oc/util

all:
	gcc $(INCLUDES) coap-parser.c ../../nexus/oc/messaging/coap/coap.c -o coap-parser-c-cli

clean: 
	rm -f coap-parser-c-cli