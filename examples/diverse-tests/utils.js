/* timeout in milliseconds */
TIMEOUT(3600000, log.testOK());

var verbose = false;
var protocols = [20, 21, 22, 23, 53, 80, 123, 161, 443, 546, 5457, 1900, 5222, 5223, 5269, 5298, 5353, 5683, 5684, 5671, 5672, 1883,8882, 8883];
var random = new java.util.Random(sim.getRandomSeed());

function getRandomPort(title) {
    var pi = random.nextInt(protocols.length + 1);
    var prot = (pi < protocols.length) ? protocols[pi] : 32768 + random.nextInt(32768);
    log.log("Using " + title + " protocol " + prot + "\n");
    return prot;
}

function f(value) {
  return (Math.round(value * 100) / 100).toFixed(2);
}

function setBool(mote, name, value) {
  var mem = new org.contikios.cooja.mote.memory.VarMemory(mote.getMemory());
  if (!mem.variableExists(name)) {
    log.log("ERR: could not find variable '" + name + "'\n");
    return false;
  }
  var symbol = mem.getVariable(name);
  if (verbose) {
    var oldValue = mem.getInt8ValueOf(symbol.addr) ? "true" : "false";
    log.log("Set bool " + name + " (address 0x" + java.lang.Long.toHexString(symbol.addr)
            + "/" + symbol.size + ": " + oldValue + ") to " + value + "\n");
  }
  mem.setInt8ValueOf(symbol.addr, value);
  return true;
}

function setInt16(mote, name, value) {
  var mem = new org.contikios.cooja.mote.memory.VarMemory(mote.getMemory());
  if (!mem.variableExists(name)) {
    log.log("ERR: could not find variable '" + name + "'\n");
    return false;
  }
  var symbol = mem.getVariable(name);
  if (verbose) {
    var oldValue = mem.getInt16ValueOf(symbol.addr) &amp; 0xffff;
    log.log("Set int16 " + name + " (address 0x" + java.lang.Long.toHexString(symbol.addr)
            + "/" + symbol.size + ": " + oldValue + ") to " + value + "\n");
  }
  mem.setInt16ValueOf(symbol.addr, value);
  return true;
}
