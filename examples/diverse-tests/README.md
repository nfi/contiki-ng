# Simulations

* rpl-udp-lite.csc - UDP data collection using RPL Lite with non-storing mode, 20 nodes, random port at startup, random payload sizes.
* rpl-udp-classic.csc - UDP data collection using RPL Classic with storing mode, 20 nodes, random port at startup, random payload sizes.
* rpl-udp-lite-varying.csc - UDP data collection using RPL Lite with non-storing mode, 20 nodes, periodic random port, random payload sizes.
* ipv6-tcp-sockets.csc - TCP transfer between two nodes using RPL Lite, random port and payload size for each TCP stream.

## Randomly selected ports for communication

Ports are randomly selected among:

* 20   - ftp
* 21   - ftp data
* 22   - SSH
* 23   - telnet
* 53   - DNS
* 80   - HTTP
* 123  - NTP
* 161  - SNMP
* 443  - HTTPS
* 546,547 - DHCPv6
* 1900 - SSDP
* 5222 - XMPP client connection (RFC 6120)
* 5223 - XMPP client connection over SSL (Unofficial)
* 5269 - XMPP server connection (RFC 6120)
* 5298 - TCP UDP XMPP JEP-0174: Link-Local Messaging
* 5353 - Multicast DNS
* 5683 - COAP
* 5684 - COAPS
* 5671 - AMPQP over TLS/SSL
* 5672 - AMQP
* 1883,8882 - MQTT
* 8883 - MQTTS
* User defined - randomly selected between 32768 and 65534

## Randomly payload sizes

* Each UDP packet has a randomly selected payload size between smallest possible message ("Hello <seqno>") and 1023.
* Each TCP stream has a randomly selected payload between 0 and 262143.
