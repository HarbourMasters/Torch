const fs = require('fs');
const data = require('./assets.json');

const header = [
    '#pragma once',
    '#include "align_asset_macro.h"',
    '',
]
const samples = []

async function main(){
    const files = Object.keys(data).filter(f => f.endsWith(".aiff") && data[f][1]['jp'] != undefined).map(m => {
        return {
            file: m,
            offset: data[m][1]['jp'][1]
        }
    })

    files.sort((a, b) => a.offset - b.offset);

    for(const entry of files){
        const sampleId = entry.offset.toString(16).padStart(2, '0').toUpperCase();
        samples.push(`gSample${sampleId}`);
        header.push(`#define dgSample${sampleId} "__OTR__${entry.file.replace('.aiff', '')}"`);
        header.push(`static const ALIGN_ASSET(2) char gSample${sampleId}[] = dgSample${sampleId};\n`);
    }

    header.push('static const char* gSampleTable[] = {');
    for (const sample of samples) {
        header.push(`    ${sample},`);
    }
    header.push('};\n');

    fs.writeFileSync('./samples_table.h', header.join('\n'));
}

main();