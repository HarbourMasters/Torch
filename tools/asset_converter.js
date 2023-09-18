const YAML = require('./yaml/dist/index')
const fs = require('fs');
const path = require('path');
const assets = require('./assets.json');
const version = 'eu';

const map = {
    png: texture,
    anime: anim,
    audio: audio,
    aiff: audio,
    bnk: audio,
    bset: audio,
    m64: audio,
    bin: blob,
    sbox: blob
}

function toYAML(obj){
    let file = `${obj.file}`;
    delete obj.file;
    return YAML.stringify({
        [file]: obj
    });
}

function texture(file, data){
    if(!data[3][version]) return;
    let format = path.parse(file);
    return {
        file: path.join(format.dir, format.name),
        type: 'TEXTURE',
        size: data[2],
        width: data[0],
        height: data[1],
        offset: data[3][version][0],
        mio0: data[3][version][1],
        format: format.ext.toUpperCase().slice(1),
    };
}

function anim(file, data){
    return {
        file: file,
        type: 'SM64:ANIM',
        header: {
            offset: data[version].header[0],
            size: data[version].header[1],
        },
        values: {
            offset: data[version].values[0],
            size: data[version].values[1],
        },
        indices: {
            offset: data[version].indices[0],
            size: data[version].indices[1],
        }
    };
}

function audio(file, data, ext){
    if(ext === 'audio'){
        if(!data[version]) return;
        return {
            file: 'header',
            type: 'AUDIO:HEADER',
            ctl: {
                offset: data[version]['sound_ctl'][0],
                size: data[version]['sound_ctl'][1],
            },
            tbl: {
                offset: data[version]['sound_tbl'][0],
                size: data[version]['sound_tbl'][1],
            },
        }
    }
    if(ext === 'aiff'){
        if(!data[1][version]) return;
        return {
            file: file,
            type: 'SAMPLE',
            id: data[1][version][1]
        };
    }
    if(ext === 'm64'){
        if(!data[version]) return;
        return {
            file: file,
            type: 'SEQUENCE',
            offset: data[version]['offset'][0],
            size: data[version]['offset'][1],
            banks: data['banks'],
        };
    }
    if(ext === 'bnk'){
        return {
            file: file,
            type: 'BANK',
            id: data[0]
        };
    }
}

function blob(file, data){
    if(!data[1][version]) return;
    let mio0 = data[1][version].length > 1;
    let output = {
        file: file,
        type: 'BLOB',
        size: data[0],
        offset: data[1][version][0],
    };
    if(mio0){
        output['mio0'] = data[1][version][1];
    }
    return output;
}

async function main(){
    let keys = Object.keys(assets).filter(f => f.includes('/'));
    let output = path.join('assets/sm64', version);
    fs.rmdirSync(output, { recursive: true });
    fs.mkdirSync(output, { recursive: true });
    for(let key of keys.sort()){
        let parent = key.split('/')[0];
        let ext = key.split('.').pop();
        let func = map[ext];
        let result = func(key.slice(parent.length + 1, key.indexOf(ext) - 1), assets[key], ext);
        if(result){
            fs.appendFileSync(`${output}/${parent}.yml`, toYAML(result));
        }
    }
}

main();