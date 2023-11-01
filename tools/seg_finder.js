const fs = require('fs')

const rom = fs.readFileSync('./baserom.us.z64')
const segTable = 0x33b400

for (let i = 0; i < 32; i++) {
    const seg = rom.readUInt32BE(segTable + (i * 4))
    console.log((seg & 0x00ffffff).toString(16))
}