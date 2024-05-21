/* timeout in milliseconds */
TIMEOUT(3600000, log.testOK());

function setTcpPort() {
    var tcp_port = getRandomPort("server");
    var motes = sim.getMotes();
    for (var i = 0; i < motes.length; i++) {
        var mote = motes[i];
        setInt16(mote, "tcp_test_port", tcp_port);
    }
    return tcp_port;
}

setTcpPort();
while (true) {
    if (msg.indexOf('Stream OK') != -1) {
        var port = setTcpPort();
        log.log("Starting new stream at port " + port + "\n");
    } else if (msg.indexOf(' ERROR ') != -1) {
        log.log(time + ":" + id + ":" + msg + "\n");
        log.testFailed();
    }
    YIELD();
}
