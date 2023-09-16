const fs = require('fs');
const data = require('../sequences.json');

const header = [
    '#pragma once',
    '',
]
const bankset = []

async function main(){
    // header.push('static const uint8_t gBankIdxMap[][16] = {');
    let idx = 0;
    for(const values of Object.values(data)){
        const indices = values.map(v => `${parseInt(v.split('_')[0], 16)}`);
        // header.push(`    // Bank: ${Object.keys(data)[idx++]}`);
        header.push(`[${indices.join(', ')}],`);
    }
    // header.push('};\n');

    fs.writeFileSync('./bankset_table.h', header.join('\n'));
}

main();