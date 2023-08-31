const fs = require('fs');

const bytes = fs.readFileSync('./baserom.us.z64');
const bpath = '../sound/sequences/us/'
const header = [
    '#pragma once',
    '#include "align_asset_macro.h"',
    '',
]
const sequences = []

fs.readdirSync(bpath).forEach(seq => {
    const seqData = fs.readFileSync(bpath+seq);
    const seqOffset = bytes.indexOf(seqData);
    const output = `sound/sequences/${seq}`
    const seqId = seq.split("_")[0]
    console.log(`"${output}": ${JSON.stringify([
        seqData.length,
        { "us": [ seqOffset ]}
    ])},`);
    sequences.push(`gSequence${seqId}`);
    header.push(`#define dgSequence${seqId} "__OTR__${output}"`);
    header.push(`static const ALIGN_ASSET(2) char gSequence${seqId}[] = dgSequence${seqId};\n`);
});

header.push('static const char* gSequenceTable[] = {');
for (const seq of sequences.sort((a, b) => b.localeCompare(a))) {
    header.push(`    ${seq},`);
}
header.push('};\n');

fs.writeFileSync('./sequences_table.h', header.join('\n'));