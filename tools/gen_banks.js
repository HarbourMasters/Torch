const fs = require('fs');
const data = require('../assets.json');

const header = [
    '#pragma once',
    '#include "align_asset_macro.h"',
    '',
]
const banks = []

async function main(){
    const files = Object.keys(data).filter(f => f.endsWith(".bnk") && data[f]['us'] != undefined).map(m => {
        return {
            file: m,
            offset: data[m]['us'][0]
        }
    })

    files.sort((a, b) => a.offset - b.offset);

    for(const entry of files){
        const sampleId = entry.offset.toString(16).padStart(2, '0').toUpperCase();
        banks.push(`gBank${sampleId}`);
        header.push(`#define dgBank${sampleId} "__OTR__${entry.file.replace('.bnk', '')}"`);
        header.push(`static const ALIGN_ASSET(2) char gBank${sampleId}[] = dgBank${sampleId};\n`);
    }

    header.push('static const char* gBankTable[] = {');
    for (const bank of banks) {
        header.push(`    ${bank},`);
    }
    header.push('};\n');

    fs.writeFileSync('./banks_table.h', header.join('\n'));
}

main();