const YAML = require('./yaml/dist/index')
const fs = require('fs');
const path = require('path');
const assets = require('../assets.json');
const version = 'jp';

const map = {
    png: texture,
    anime: anim,
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
    return {
        file: file,
        type: 'BLOB',
        size: data[0],
        offset: data[1][version][0],
        mio0: data[1][version][1] == 0,
    };
}

async function main(){
    let keys = Object.keys(assets).filter(f => f.includes('/'));
    let output = path.join('output', version);
    fs.rmdirSync(output, { recursive: true });
    fs.mkdirSync(output);
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