import createModule from './torch.js';

let SelectedRom = undefined;
let Module = undefined;
let RomBuffer = undefined;
let Countries = [
    "Japan",
    "North America",
    "Europe",
    "Unknown"
]

document.addEventListener('DOMContentLoaded', async () => {
    Module = await createModule();
});

function ToggleModal(show) {
  document.getElementById('loadingModal').style.display = show ? 'flex' : 'none';
}

function UpdateStatus(message) {
    const statusElement = document.querySelector('.status');
    statusElement.innerHTML = message;
}

function Sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function downloadFile(FS, path) {
    const buffer = FS.readFile(path);
    const blob = new Blob([buffer], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = path.split('/').pop();
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

function readRomFile(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = function(event) {
            const arrayBuffer = event.target.result;
            const byteArray = new Uint8Array(arrayBuffer);
            resolve(byteArray);
        };
        reader.onerror = function(event) {
            reject(new Error("Failed to read ROM file: " + event.target.error));
        };
        reader.readAsArrayBuffer(file);
    });
}

async function convertToO2R() {
    const { FS, Companion, ArchiveType, ExportType, Bind, RegisterLogSink } = Module;

    const game = document.getElementById('starship-config').checked ? 'sf64' : 'mk64';
    const companion = new Companion(
        RomBuffer,
        ArchiveType.O2R,
        false, false,
        `assets/${game}`,
        "output"
    );

    ToggleModal(true);
    await Sleep(2000);
    Bind(companion);
    RegisterLogSink((message) => {
        if(message.includes("No config")) {
            UpdateStatus("Unsupported ROM, please select a valid ROM file.");
        } else {
            console.log(message);
        }
    });

    const start = Date.now();
    companion.Init(ExportType.Binary);
    companion.Process();
    companion.Finalize(start);
    await downloadFile(FS, companion.GetOutputPath());
    UpdateStatus(`Conversion complete!`);
    ToggleModal(false);
}

document.getElementById('romInput').addEventListener('change', async (event) => {
    if (event.target.files.length > 0) {
        const { Cartridge, VectorUInt8 } = Module;

        SelectedRom = event.target.files[0];
        RomBuffer = new VectorUInt8();

        const rom = await readRomFile(SelectedRom);
        rom.forEach(byte => RomBuffer.push_back(byte));

        const cartridge = new Cartridge(RomBuffer);
        cartridge.Initialize();

        const title = cartridge.GetGameTitle().trim();
        const hash = cartridge.GetHash();
        const region = Countries[cartridge.GetCountry().value];

        UpdateStatus(`Title: ${title}<br>Region: ${region}<br>Hash: ${hash}<br>File: ${SelectedRom.name}`);
    }
});

document.getElementById('start-btn').addEventListener('click', () => {
    if (!RomBuffer) {
        alert("Please select a ROM file first.");
        return;
    }
    convertToO2R();
});

document.getElementById('rom-picker').addEventListener('click', () => {
    document.getElementById('romInput').click();
});