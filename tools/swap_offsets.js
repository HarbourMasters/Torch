const YAML = require('./yaml/dist/index')
const fs = require('fs')

const parent = 'assets/sm64/us'

for(let file of fs.readdirSync(parent)){
    const data = fs.readFileSync(`${parent}/${file}`)
    const yaml = YAML.parse(data.toString())
    const keys = Object.keys(yaml)
    for(let key of keys) {
        const entry = yaml[key]
        let mio0 = entry.mio0
        if(!mio0) continue
        let offset = entry.offset
        console.log('MIO:', mio0, 'OFFSET:', offset, key)
        // mio0 = parseInt(mio0)
        // offset = parseInt(offset)
        // if(offset < mio0) continue
        // entry.offset = `0x${mio0.toString(16).toUpperCase()}`
        // entry.mio0 = `0x${offset.toString(16).toUpperCase()}`
        // console.log(entry)
        // yaml[key] = entry
    }

    const output = YAML.stringify(yaml).replace(/['"]+/g, '')

    // fs.writeFileSync(`${parent}/${file}`, output)
}