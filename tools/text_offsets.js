const fs = require('fs');
const path = require('path');
const YAML = require('./yaml/dist/index')

// https://hack64.net/wiki/doku.php?id=super_mario_64:mio0
// https://hack64.net/wiki/doku.php?id=super_mario_64:segment_2

const version = 'us';
const roms = {
    us: {
        dialog: { start: 0x0FFC8, end: 0x10A68 },
        level: { start: 0x10F68, end: 0x10FD4 },
        act: { start: 0x1192C, end: 0x11AB4 },
    }
}

function toYAML(obj){
    let file = `${obj.file}`;
    delete obj.file;
    return YAML.stringify({
        [file]: obj
    });
}

function align16(num){
    return (num + 0xF) & ~0xF;
}

function seg_offset(a){
    return a & 0x00ffffff
}

function readEntry(bytes, offset){
    let size = 0;
    let unused = bytes.readUInt32BE(offset + size);
    size += 4;
    let linesPerBox = bytes.readInt8(offset + size);
    size += 2;
    let leftOffset = bytes.readInt16BE(offset + size);
    size += 2;
    let width = bytes.readInt16BE(offset + size);
    size += 4;
    let str = seg_offset(bytes.readInt32BE(offset + size));
    size += 4;

    let outText = []
    while(bytes[str] != 0xFF){
        let c = bytes[str];
        outText.push(c);
        str += 1;
    }
    outText.push(0xFF)

    return {
        unused,
        linesPerBox,
        leftOffset,
        width,
        text: Buffer.from(outText),
    };
}

function findDialogs(bytes, out){
    let { start, end } = roms[version].dialog;
    let size = align16(end - start);

    for(let i = 0; i < size; i += 16){
        fs.appendFileSync(out, toYAML({
            file: `dialogs/dialog_${(i / 16).toString(16).padStart(2, '0').toUpperCase()}`,
            type: 'SM64:DIALOG',
            offset: 0x108A40,
            mio0: start + i
        }));

        console.log(i / 16, readEntry(bytes, start + i));
    }
}

function readLevel(bytes, offset){
    let outText = []
    while(bytes[offset] != 0xFF){
        let c = bytes[offset];
        outText.push(c);
        offset += 1;
    }
    outText.push(0xFF)
    return Buffer.from(outText);
}

function findLevelNames(bytes, out) {
    let { start, end } = roms[version].level;
    let size = ((end - start) / 4) - 1;
    console.log(size)

    for(let i = 0; i < size; i++){
        let offset = seg_offset(bytes.readUInt32BE(start + (i * 4)));
        fs.appendFileSync(out, toYAML({
            file: `courses/course_${(i).toString(16).padStart(2, '0').toUpperCase()}`,
            type: 'SM64:TEXT',
            offset: 0x108A40,
            mio0: offset
        }));
    }
}

function findAct(bytes, out) {
    let { start, end } = roms[version].act;
    let size = ((end - start) / 4) - 1;
    console.log(size)
    for(let i = 0; i < size; i++){
        let offset = seg_offset(bytes.readUInt32BE(start + (i * 4)));
        fs.appendFileSync(out, toYAML({
            file: `acts/act_${(i).toString(16).padStart(2, '0').toUpperCase()}`,
            type: 'SM64:TEXT',
            offset: 0x108A40,
            mio0: offset
        }));
    }
}

async function main(){
    let bytes = fs.readFileSync('./debug/text/text/dump');
    let output = path.join('assets/sm64/us', 'texts.yml');
    fs.writeFileSync(output, '');
    findDialogs(bytes, output);
    findLevelNames(bytes, output);
    findAct(bytes, output);
}

main();