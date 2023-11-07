const YAML = require('./yaml/dist/index')
const fs = require('fs')
const path = require('path')

const input = '/Volumes/Moon/dot/HM64/bkp/Ghostship/levels/intro/leveldata.c'
const output = '/Volumes/Moon/dot/HM64/sm64-cc0/CubeOS/assets/sm64/us/levels/intro.yml'
const mio0 = '0x26A3A0'

function seg_offset(a){
    return a & 0x00ffffff
}

function toYAML(obj){
    let file = `${obj.file}`;
    delete obj.file;
    return YAML.stringify({
        [file]: obj
    });
}

function sanitize(line){
    return line.replace(/(?:const) /gm, '')
               .replace(/(?:static) /gm, '')
               .replace(/(?:ALIGNED8) /gm, '')
}

async function main(){
    const data = fs.readFileSync(input);
    const lines = data.toString().split('\n').filter((x) => x.trim().length > 0);
    for(let idx = 0; idx < lines.length; idx++){
        let line = lines[idx];

        if(!line.startsWith('// ')) continue;
        line = line.replace('// ', '');
        let variable = sanitize(lines[idx + 1]);
        let type = variable.split(' ')[0];

        if(!['Vtx', 'Gfx'].includes(type)) continue;

        let name = variable.split(' ')[1].split('[')[0];
        let start = seg_offset(parseInt(line.replace('0x', '').split(' - ')[0], 16));
        let end   = seg_offset(parseInt(line.replace('0x', '').split(' - ')[1], 16));
        let buffer;

        switch(type) {
            case 'Vtx':
                buffer = toYAML({
                    file: name,
                    type: 'VTX',
                    mio0: mio0,
                    offset: `0x${start.toString(16)}`,
                    count: (end - start) / 16,
                    symbol: name,
                });
                break;
            case 'Gfx':
                buffer = toYAML({
                    file: name,
                    type: 'GFX',
                    mio0: mio0,
                    offset: `0x${start.toString(16)}`,
                    symbol: name,
                });
                break;
        }

        buffer = buffer.trim().replace(/"/g, '')
        fs.appendFileSync(output, '\n' + buffer);
    }
}

main()