const fs = require('fs');
const path = require('path');

let _parent = 'assets/reordered';
let bytes = Buffer.alloc(0);

function toHex(value) {
    let hex = value.toString(16);
    if ((hex.length % 2) > 0) {
        hex = "0" + hex;
    }
    return `0x${hex.toUpperCase()}`;
}

function toBEU16(num){
    let d = new Uint16Array(1);
    d[0] = num;
    return Buffer.from(d.buffer).swap16()
}

async function parseAnim(file){
    let fimport = `./${file.replace('.js', '')}`
    let data = require(fimport)
    for(let id of fimport.split('_').slice(1)){
        let anim = data['anim_'+id];
        let hOffset = bytes.indexOf(Buffer.concat(anim.slice(0, 6).map(toBEU16)));

        let bswValues = Buffer.concat(anim[6].map(toBEU16));
        let vsOffset = bytes.indexOf(bswValues);
        let bswIndices = Buffer.concat(anim[7].map(toBEU16));
        let idxOffset = bytes.indexOf(bswIndices);
        console.log(`"assets/anims/anim_${id}.anim": ${JSON.stringify({
            header: [hOffset, 0x18],
            values: [vsOffset, bswValues.length],
            indices: [idxOffset, bswIndices.length],
        })},`);
    }
}

async function main(){
    bytes = fs.readFileSync('./baserom.us.z64');
    for(let file of fs.readdirSync(_parent)){
        let pdir = path.join(_parent, file);
        let lines = fs.readFileSync(pdir, 'utf8').split('\n').map((line) =>
            line.replace('const u16', 'let')
                .replace('const s16', 'let')
                .replace('#include "animation_table.h"', '')
                .replace('const struct Animation', 'let')
                .replace('[] = {', ' = [')
                .replace('};', ']')
        );
        let func = "function ANIMINDEX_NUMPARTS(animindex) {\n    return Math.floor(animindex.length / 6) - 1;\n}".split('\n');
            let output = path.join('assets/js', file).replace('.inc.c', '.js');
        fs.writeFileSync(output, [
            ...func,
            ...lines,
            `module.exports = { ${output.replace('.js', '').split('_').slice(1).map(m => `anim_${m}`).join(', ')} };`,
        ].join('\n'));
        await parseAnim(output);
    }
}

main();