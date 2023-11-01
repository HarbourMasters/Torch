const YAML = require('./yaml/dist/index')
const fs = require('fs')
const path = require('path')

const version = 'us'
const parent = 'assets/sm64/' + version
const symbols = {}
const rgx = /^-?\d+(\.\d+)?:.*/;

function toYAML(obj){
    let file = `${obj.file}`;
    delete obj.file;
    return YAML.stringify({
        [file]: obj
    });
}

async function dirscan(folder, parent){
    let tmp = parent ? parent : [];

    if(!fs.statSync(folder).isDirectory()) return;

    const files = fs.readdirSync(folder);
    for(const file of files){
        const fpath = path.join(folder, file);
        tmp.push(fpath);
        await dirscan(fpath, tmp);
    }

    return tmp;
}

async function scanSymbols(){
    const files = (await dirscan('../../bkp/Ghostship')).filter((x) => x.endsWith('.c'));
    for(let file of files){
        const data = fs.readFileSync(file);
        const lines = data.toString().split('\n');
        for(let line of lines){
            if((line.includes('Texture ') || line.includes('u8 ')) && line.includes('__OTR__')) {
                let symbol = line.split(line.includes('u8') ? 'u8 ' : 'Texture ')[1].split('[]')[0].trim();
                let name = line.split('__OTR__')[1].replace('";', '').trim();
                if(!symbols[name]){
                    symbols[name] = symbol;
                }
            }
        }
    }
}

function patchName(parent, name){
    if(name.length > 2) return name;
    try {
        if(name == '0' || parseInt(name, 16)){
            if(parent.includes('samples'))
                return `sfx_${name}`;
            if(parent.includes('banks'))
                return `bnk_${name}`;
            return `${parent}_${name}`;
        }
    } catch(e) {
        console.log(e);
    }
    return name;
}

function patchEntry(entry, key){
    let obj = entry;
    if(obj.type == 'SEQUENCE'){
        let banks = obj.banks;
        if(banks) {
            obj.banks = banks.map((x) => {
                let parse = path.parse(x);
                return path.join(parse.dir, patchName(parse.dir, parse.name));
            });
        }
    }
    if(obj.type == 'TEXTURE'){
        if(symbols[key]) {
            obj.symbol = symbols[key];
        } else {
            console.log(`Missing symbol for ${key}`);
        }
    }

    if(obj.type != 'BLOB'){
        let mio0 = obj.mio0;
        let offset = obj.offset;

        if(mio0 != undefined && offset != undefined) {
            obj.offset = mio0;
            obj.mio0 = offset;
        }
    }

    if(typeof obj.offset == 'number'){
        obj.offset = `0x${obj.offset.toString(16).toUpperCase()}`;
    }
    if(typeof obj.mio0 == 'number'){
        obj.mio0 = `0x${obj.mio0.toString(16).toUpperCase()}`;
    }
    return obj;
}

async function main(){
    await scanSymbols();
    for(let file of fs.readdirSync(parent)){
        const data = fs.readFileSync(`${parent}/${file}`)
        const yaml = YAML.parse(data.toString())
        const keys = Object.keys(yaml)
        const found = {}
        for(let key of keys) {
            const parent = path.dirname(key);
            if(!found[parent]) found[parent] = []
            let f = path.relative(parent, key);
            found[parent].push({
                file: patchName(parent, f, file),
                ...patchEntry(yaml[key], path.join(file.split('.')[0], key))
            })
        }

        for(let key of Object.keys(found)) {
            let yml = `generated/${version}/${path.parse(file).name}/${key == '.' ? 'root' : key}.yml`;
            fs.mkdirSync(path.dirname(yml), {recursive: true})
            let txt = found[key].map((x) => toYAML(x).replace(/['"]+/g, '')).join('')
            fs.writeFileSync(yml, txt)
        }
    }
}

main()