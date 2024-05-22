/* timeout in milliseconds */
TIMEOUT(14400000, log.testOK());

function setTcpPort(tcp_port) {
    var motes = sim.getMotes();
    for (var i = 0; i < motes.length; i++) {
        var mote = motes[i];
        setInt16(mote, "tcp_test_port", tcp_port);
    }
    return tcp_port;
}

/* use MQTT port */
setTcpPort(1883);
while (true) {
    YIELD();
    if (msg.indexOf(' ERROR ') != -1) {
        log.log(time + ":" + id + ":" + msg + "\n");
        log.testFailed();
    }
}
