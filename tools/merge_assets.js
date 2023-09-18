const fs = require('fs');
const path = require('path');
const assets = require('../assets.json');
const files = {}

async function main(){
    const m64s = Object.keys(assets).filter(f => f.endsWith('.m64'));
    for(let entry of m64s){
        if(entry.includes('sh')) continue;
        const size = assets[entry][0];
        const parent = assets[entry][1];
        const keys = Object.keys(parent);
        const offset = parent[keys[0]][0];
        const key = `sound/sequences/${path.basename(entry)}`
        if(!files[key]){
            files[key] = {
                eu: {},
                us: {},
                jp: {},
                banks: []
            }
        }
        const output = {
            offset: [offset, size]
        }

        if(keys.length > 1){
            files[key][keys[1]] = parent[keys[1]];
        }
        files[key][keys[0]] = output;
    }
    for(let entry of Object.keys(files)){
        console.log(`"${entry}": ${JSON.stringify(files[entry])},`);
    }
}

main();