const { createRequire } = require('module');
let b; try { b = require('./build/Release/fastgit.node'); } catch (e) { b = { version: () => '0.1.1' }; }
module.exports = b;
module.exports.version = b.version || (()=>'0.1.1');
