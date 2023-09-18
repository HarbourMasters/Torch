const fs = require('fs');

const bytes = fs.readFileSync('./baserom.eu.z64');
const file = fs.readFileSync('./00_sound_player_eu.m64');

const offset = bytes.indexOf(file);
const size = file.length;

console.log(offset, size)