/* udp client/server */

var server_port = getRandomPort("server");
var client_port = getRandomPort("client");
var motes = sim.getMotes();
for (var i = 0; i < motes.length; i++) {
    var mote = motes[i];
    setInt16(mote, "udp_server_port", server_port);
    setInt16(mote, "udp_client_port", client_port);
}
