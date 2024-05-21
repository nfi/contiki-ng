/* timeout in milliseconds */
TIMEOUT(7200000, log.testOK());

function reset_ports() {
    var motes = sim.getMotes();
    for (var i = 0; i < motes.length; i++) {
        var mote = motes[i];
        setInt16(mote, "udp_server_port", 0);
        setInt16(mote, "udp_client_port", 0);
    }
}

function update_ports() {
    var server_port = getRandomPort("server");
    var client_port = getRandomPort("client");
    var motes = sim.getMotes();
    for (var i = 0; i < motes.length; i++) {
        var mote = motes[i];
        setInt16(mote, "udp_server_port", server_port);
        setInt16(mote, "udp_client_port", client_port);
    }
}

while (true) {
    update_ports();
    GENERATE_MSG(300000, "continue");
    YIELD_THEN_WAIT_UNTIL(msg.equals("continue"));
    log.log("Reset ports\n");
    reset_ports();
    GENERATE_MSG(15000, "continue");
    YIELD_THEN_WAIT_UNTIL(msg.equals("continue"));
}
