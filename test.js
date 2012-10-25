var DMX = require('./dmx_native.node');
var list = DMX.list();
console.log('Found ' + list.length + ' devices');

if (list.length > 0) {
  try {
    var dmx = DMX.DMX(0);
  } catch (e) {
    console.error(e.message);
    process.exit();
  }
  dmx.setHz(20);
  dmx.step(5);
  var val = [255], odd=true;
  setInterval(function(){
    dmx.set(val);
    val.unshift(odd =! odd ? 255 : 0);
    if (val.length > 512) val.pop();
  }, 3000);
}
