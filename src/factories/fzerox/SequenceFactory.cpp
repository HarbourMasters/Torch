#include "SequenceFactory.h"
#include "spdlog/spdlog.h"

#include "Companion.h"
#include "utils/Decompressor.h"
#include "utils/TorchUtils.h"
#include <deque>

// control flow commands
#define ASEQ_OP_CONTROL_FLOW_FIRST 0xF2
#define ASEQ_OP_RBLTZ   0xF2
#define ASEQ_OP_RBEQZ   0xF3
#define ASEQ_OP_RJUMP   0xF4
#define ASEQ_OP_BGEZ    0xF5
#define ASEQ_OP_BREAK   0xF6
#define ASEQ_OP_LOOPEND 0xF7
#define ASEQ_OP_LOOP    0xF8
#define ASEQ_OP_BLTZ    0xF9
#define ASEQ_OP_BEQZ    0xFA
#define ASEQ_OP_JUMP    0xFB
#define ASEQ_OP_CALL    0xFC
#define ASEQ_OP_DELAY   0xFD
#define ASEQ_OP_DELAY1  0xFE
#define ASEQ_OP_END     0xFF

// sequence commands
#define ASEQ_OP_SEQ_TESTCHAN        0x00 // low nibble used as argument
#define ASEQ_OP_SEQ_STOPCHAN        0x40 // low nibble used as argument
#define ASEQ_OP_SEQ_SUBIO           0x50 // low nibble used as argument
#define ASEQ_OP_SEQ_LDRES           0x60 // low nibble used as argument
#define ASEQ_OP_SEQ_STIO            0x70 // low nibble used as argument
#define ASEQ_OP_SEQ_LDIO            0x80 // low nibble used as argument
#define ASEQ_OP_SEQ_LDCHAN          0x90 // low nibble used as argument
#define ASEQ_OP_SEQ_RLDCHAN         0xA0 // low nibble used as argument
#define ASEQ_OP_SEQ_LDSEQ           0xB0 // low nibble used as argument
#define ASEQ_OP_SEQ_RUNSEQ          0xC4
#define ASEQ_OP_SEQ_SCRIPTCTR       0xC5
#define ASEQ_OP_SEQ_STOP            0xC6
#define ASEQ_OP_SEQ_STSEQ           0xC7
#define ASEQ_OP_SEQ_SUB             0xC8
#define ASEQ_OP_SEQ_AND             0xC9
#define ASEQ_OP_SEQ_LDI             0xCC
#define ASEQ_OP_SEQ_DYNCALL         0xCD
#define ASEQ_OP_SEQ_RAND            0xCE
#define ASEQ_OP_SEQ_NOTEALLOC       0xD0
#define ASEQ_OP_SEQ_LDSHORTGATEARR  0xD1
#define ASEQ_OP_SEQ_LDSHORTVELARR   0xD2
#define ASEQ_OP_SEQ_MUTEBHV         0xD3
#define ASEQ_OP_SEQ_MUTE            0xD4
#define ASEQ_OP_SEQ_MUTESCALE       0xD5
#define ASEQ_OP_SEQ_FREECHAN        0xD6
#define ASEQ_OP_SEQ_INITCHAN        0xD7
#define ASEQ_OP_SEQ_VOLSCALE        0xD9
#define ASEQ_OP_SEQ_VOLMODE         0xDA
#define ASEQ_OP_SEQ_VOL             0xDB
#define ASEQ_OP_SEQ_TEMPOCHG        0xDC
#define ASEQ_OP_SEQ_TEMPO           0xDD
#define ASEQ_OP_SEQ_RTRANSPOSE      0xDE
#define ASEQ_OP_SEQ_TRANSPOSE       0xDF
#define ASEQ_OP_SEQ_EF              0xEF
#define ASEQ_OP_SEQ_FREENOTELIST    0xF0
#define ASEQ_OP_SEQ_ALLOCNOTELIST   0xF1

// channel commands
#define ASEQ_OP_CHAN_CDELAY         0x00 // low nibble used as argument
#define ASEQ_OP_CHAN_LDSAMPLE       0x10 // low nibble used as argument
#define ASEQ_OP_CHAN_LDCHAN         0x20 // low nibble used as argument
#define ASEQ_OP_CHAN_STCIO          0x30 // low nibble used as argument
#define ASEQ_OP_CHAN_LDCIO          0x40 // low nibble used as argument
#define ASEQ_OP_CHAN_SUBIO          0x50 // low nibble used as argument
#define ASEQ_OP_CHAN_LDIO           0x60 // low nibble used as argument
#define ASEQ_OP_CHAN_STIO           0x70 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_RLDLAYER       0x78 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_TESTLAYER      0x80 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_LDLAYER        0x88 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_DELLAYER       0x90 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_DYNLDLAYER     0x98 // lower 3 bits used as argument
#define ASEQ_OP_CHAN_LDFILTER       0xB0
#define ASEQ_OP_CHAN_FREEFILTER     0xB1
#define ASEQ_OP_CHAN_LDSEQTOPTR     0xB2
#define ASEQ_OP_CHAN_FILTER         0xB3
#define ASEQ_OP_CHAN_PTRTODYNTBL    0xB4
#define ASEQ_OP_CHAN_DYNTBLTOPTR    0xB5
#define ASEQ_OP_CHAN_DYNTBLV        0xB6
#define ASEQ_OP_CHAN_RANDTOPTR      0xB7
#define ASEQ_OP_CHAN_RAND           0xB8
#define ASEQ_OP_CHAN_RANDVEL        0xB9
#define ASEQ_OP_CHAN_RANDGATE       0xBA
#define ASEQ_OP_CHAN_COMBFILTER     0xBB
#define ASEQ_OP_CHAN_PTRADD         0xBC
#define ASEQ_OP_CHAN_SAMPLESTART    0xBD
#define ASEQ_OP_CHAN_INSTR          0xC1
#define ASEQ_OP_CHAN_DYNTBL         0xC2
#define ASEQ_OP_CHAN_SHORT          0xC3
#define ASEQ_OP_CHAN_NOSHORT        0xC4
#define ASEQ_OP_CHAN_DYNTBLLOOKUP   0xC5
#define ASEQ_OP_CHAN_FONT           0xC6
#define ASEQ_OP_CHAN_STSEQ          0xC7
#define ASEQ_OP_CHAN_SUB            0xC8
#define ASEQ_OP_CHAN_AND            0xC9
#define ASEQ_OP_CHAN_MUTEBHV        0xCA
#define ASEQ_OP_CHAN_LDSEQ          0xCB
#define ASEQ_OP_CHAN_LDI            0xCC
#define ASEQ_OP_CHAN_STOPCHAN       0xCD
#define ASEQ_OP_CHAN_LDPTR          0xCE
#define ASEQ_OP_CHAN_STPTRTOSEQ     0xCF
#define ASEQ_OP_CHAN_EFFECTS        0xD0
#define ASEQ_OP_CHAN_NOTEALLOC      0xD1
#define ASEQ_OP_CHAN_SUSTAIN        0xD2
#define ASEQ_OP_CHAN_BEND           0xD3
#define ASEQ_OP_CHAN_REVERB         0xD4
#define ASEQ_OP_CHAN_VIBFREQ        0xD7
#define ASEQ_OP_CHAN_VIBDEPTH       0xD8
#define ASEQ_OP_CHAN_RELEASERATE    0xD9
#define ASEQ_OP_CHAN_ENV            0xDA
#define ASEQ_OP_CHAN_TRANSPOSE      0xDB
#define ASEQ_OP_CHAN_PANWEIGHT      0xDC
#define ASEQ_OP_CHAN_PAN            0xDD
#define ASEQ_OP_CHAN_FREQSCALE      0xDE
#define ASEQ_OP_CHAN_VOL            0xDF
#define ASEQ_OP_CHAN_VOLEXP         0xE0
#define ASEQ_OP_CHAN_VIBFREQGRAD    0xE1
#define ASEQ_OP_CHAN_VIBDEPTHGRAD   0xE2
#define ASEQ_OP_CHAN_VIBDELAY       0xE3
#define ASEQ_OP_CHAN_DYNCALL        0xE4
#define ASEQ_OP_CHAN_REVERBIDX      0xE5
#define ASEQ_OP_CHAN_SAMPLEBOOK     0xE6
#define ASEQ_OP_CHAN_LDPARAMS       0xE7
#define ASEQ_OP_CHAN_PARAMS         0xE8
#define ASEQ_OP_CHAN_NOTEPRI        0xE9
#define ASEQ_OP_CHAN_STOP           0xEA
#define ASEQ_OP_CHAN_FONTINSTR      0xEB
#define ASEQ_OP_CHAN_VIBRESET       0xEC
#define ASEQ_OP_CHAN_GAIN           0xED
#define ASEQ_OP_CHAN_BENDFINE       0xEE
#define ASEQ_OP_CHAN_EF         0xEF
#define ASEQ_OP_CHAN_FREENOTELIST   0xF0
#define ASEQ_OP_CHAN_ALLOCNOTELIST  0xF1

// layer commands
#define ASEQ_OP_LAYER_NOTEDVG       0x00
#define ASEQ_OP_LAYER_NOTEDV        0x40
#define ASEQ_OP_LAYER_NOTEVG        0x80
#define ASEQ_OP_LAYER_LDELAY        0xC0
#define ASEQ_OP_LAYER_SHORTVEL      0xC1
#define ASEQ_OP_LAYER_TRANSPOSE     0xC2
#define ASEQ_OP_LAYER_SHORTDELAY    0xC3
#define ASEQ_OP_LAYER_LEGATO        0xC4
#define ASEQ_OP_LAYER_NOLEGATO      0xC5
#define ASEQ_OP_LAYER_INSTR         0xC6
#define ASEQ_OP_LAYER_PORTAMENTO    0xC7
#define ASEQ_OP_LAYER_NOPORTAMENTO  0xC8
#define ASEQ_OP_LAYER_SHORTGATE     0xC9
#define ASEQ_OP_LAYER_NOTEPAN       0xCA
#define ASEQ_OP_LAYER_ENV           0xCB
#define ASEQ_OP_LAYER_NODRUMPAN     0xCC
#define ASEQ_OP_LAYER_STEREO        0xCD
#define ASEQ_OP_LAYER_BENDFINE      0xCE
#define ASEQ_OP_LAYER_RELEASERATE   0xCF
#define ASEQ_OP_LAYER_LDSHORTVEL    0xD0 // low nibble used as an argument
#define ASEQ_OP_LAYER_LDSHORTGATE   0xE0 // low nibble used as an argument

#define PITCH_A0       0
#define PITCH_BF0      1
#define PITCH_B0       2
#define PITCH_C1       3
#define PITCH_DF1      4
#define PITCH_D1       5
#define PITCH_EF1      6
#define PITCH_E1       7
#define PITCH_F1       8
#define PITCH_GF1      9
#define PITCH_G1       10
#define PITCH_AF1      11
#define PITCH_A1       12
#define PITCH_BF1      13
#define PITCH_B1       14
#define PITCH_C2       15
#define PITCH_DF2      16
#define PITCH_D2       17
#define PITCH_EF2      18
#define PITCH_E2       19
#define PITCH_F2       20
#define PITCH_GF2      21
#define PITCH_G2       22
#define PITCH_AF2      23
#define PITCH_A2       24
#define PITCH_BF2      25
#define PITCH_B2       26
#define PITCH_C3       27
#define PITCH_DF3      28
#define PITCH_D3       29
#define PITCH_EF3      30
#define PITCH_E3       31
#define PITCH_F3       32
#define PITCH_GF3      33
#define PITCH_G3       34
#define PITCH_AF3      35
#define PITCH_A3       36
#define PITCH_BF3      37
#define PITCH_B3       38
#define PITCH_C4       39
#define PITCH_DF4      40
#define PITCH_D4       41
#define PITCH_EF4      42
#define PITCH_E4       43
#define PITCH_F4       44
#define PITCH_GF4      45
#define PITCH_G4       46
#define PITCH_AF4      47
#define PITCH_A4       48
#define PITCH_BF4      49
#define PITCH_B4       50
#define PITCH_C5       51
#define PITCH_DF5      52
#define PITCH_D5       53
#define PITCH_EF5      54
#define PITCH_E5       55
#define PITCH_F5       56
#define PITCH_GF5      57
#define PITCH_G5       58
#define PITCH_AF5      59
#define PITCH_A5       60
#define PITCH_BF5      61
#define PITCH_B5       62
#define PITCH_C6       63
#define PITCH_DF6      64
#define PITCH_D6       65
#define PITCH_EF6      66
#define PITCH_E6       67
#define PITCH_F6       68
#define PITCH_GF6      69
#define PITCH_G6       70
#define PITCH_AF6      71
#define PITCH_A6       72
#define PITCH_BF6      73
#define PITCH_B6       74
#define PITCH_C7       75
#define PITCH_DF7      76
#define PITCH_D7       77
#define PITCH_EF7      78
#define PITCH_E7       79
#define PITCH_F7       80
#define PITCH_GF7      81
#define PITCH_G7       82
#define PITCH_AF7      83
#define PITCH_A7       84
#define PITCH_BF7      85
#define PITCH_B7       86
#define PITCH_C8       87
#define PITCH_DF8      88
#define PITCH_D8       89
#define PITCH_EF8      90
#define PITCH_E8       91
#define PITCH_F8       92
#define PITCH_GF8      93
#define PITCH_G8       94
#define PITCH_AF8      95
#define PITCH_A8       96
#define PITCH_BF8      97
#define PITCH_B8       98
#define PITCH_C9       99
#define PITCH_DF9      100
#define PITCH_D9       101
#define PITCH_EF9      102
#define PITCH_E9       103
#define PITCH_F9       104
#define PITCH_GF9      105
#define PITCH_G9       106
#define PITCH_AF9      107
#define PITCH_A9       108
#define PITCH_BF9      109
#define PITCH_B9       110
#define PITCH_C10      111
#define PITCH_DF10     112
#define PITCH_D10      113
#define PITCH_EF10     114
#define PITCH_E10      115
#define PITCH_F10      116
#define PITCH_BFNEG1   117
#define PITCH_BNEG1    118
#define PITCH_C0       119
#define PITCH_DF0      120
#define PITCH_D0       121
#define PITCH_EF0      122
#define PITCH_E0       123
#define PITCH_F0       124
#define PITCH_GF0      125
#define PITCH_G0       126
#define PITCH_AF0      127

#define READ_U8 (((count++), (reader.ReadUByte())))
#define READ_S16 (((count+=2), (reader.ReadUInt16())))
#define READ_CS16 (((count++), (temp = reader.ReadUByte(), (temp & 0x80) ? (count++, ((temp & 0x7F) << 8) | reader.ReadUByte()) : temp)))

#define FORMAT_HEX(x, w) "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec
#define FORMAT_HEX2(x, w) std::hex << std::uppercase << std::setfill('0') << std::setw(w) << x << std::nouppercase << std::dec

ExportResult FZX::SequenceHeaderExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    return std::nullopt;
}

ExportResult FZX::SequenceCodeExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto data = std::static_pointer_cast<SequenceData>(raw);

    write << "u8 " << symbol << "[] = {\n";

    std::sort(data->mCmds.begin(), data->mCmds.end());

    uint32_t lastEndPos = 0;
    for (const auto& command : data->mCmds) {
        if (lastEndPos != command.pos) {
            write << fourSpaceTab << "/* Missing instruction at: " << FORMAT_HEX(lastEndPos, 3) << " ( With Gap " << FORMAT_HEX(command.pos - lastEndPos, 2) << " )*/\n";
        }
        lastEndPos = command.pos + command.size;

        if (data->mLabels.contains(command.pos)) {
            write << "// L____" << FORMAT_HEX2(command.pos, 3) << ":\n";
        }

        if (command.state == SequenceState::data) {
            uint32_t i = 0;
            write << fourSpaceTab << "/* DATA LABELS */\n";
            for (const auto& arg: command.args) {
                write << fourSpaceTab << "/* " << FORMAT_HEX(command.pos + 2 * i, 3) << " - " << FORMAT_HEX(command.pos + 2 * (i + 1), 3) << " */ ";
                write << FORMAT_HEX(std::get<uint16_t>(arg), 3) << ", ";
                write << "// L____" << FORMAT_HEX2(std::get<uint16_t>(arg), 3) << "\n";
                i++;
            }
            write << fourSpaceTab << "/* DATA END */\n";
            continue;
        }

        write << fourSpaceTab << "/* " << FORMAT_HEX(command.pos, 3) << " - " << FORMAT_HEX(command.pos + command.size, 3) << " */ ";

        if (command.channel != -1) {
            write << "/* Channel: " << command.channel << " */ ";
            if (command.layer != -1) {
                write << "/* Layer: " << command.layer << " */ ";
            }
        }
        switch (command.cmd) {
            case ASEQ_OP_RBLTZ:
                write << "ASEQ_OP_RBLTZ,";
                break;
            case ASEQ_OP_RBEQZ:
                write << "ASEQ_OP_RBEQZ,";
                break;
            case ASEQ_OP_RJUMP:
                write << "ASEQ_OP_RJUMP,";
                break;
            case ASEQ_OP_BGEZ:
                write << "ASEQ_OP_BGEZ,";
                break;
            case ASEQ_OP_BREAK:
                write << "ASEQ_OP_BREAK,";
                break;
            case ASEQ_OP_LOOPEND:
                write << "ASEQ_OP_LOOPEND,";
                break;
            case ASEQ_OP_LOOP:
                write << "ASEQ_OP_LOOP,";
                break;
            case ASEQ_OP_BLTZ:
                write << "ASEQ_OP_BLTZ,";
                break;
            case ASEQ_OP_BEQZ:
                write << "ASEQ_OP_BEQZ,";
                break;
            case ASEQ_OP_JUMP:
                write << "ASEQ_OP_JUMP,";
                break;
            case ASEQ_OP_CALL:
                write << "ASEQ_OP_CALL,";
                break;
            case ASEQ_OP_DELAY:
                write << "ASEQ_OP_DELAY,";
                break;
            case ASEQ_OP_DELAY1:
                write << "ASEQ_OP_DELAY1,";
                break;
            case ASEQ_OP_END:
                write << "ASEQ_OP_END,";
                break;
        }
        if (command.cmd < ASEQ_OP_CONTROL_FLOW_FIRST) {
            if (command.state == SequenceState::player) {
                if (command.cmd >= 0xC0) {
                    switch (command.cmd) {
                        case ASEQ_OP_SEQ_RUNSEQ:
                            write << "ASEQ_OP_SEQ_RUNSEQ,";
                            break;
                        case ASEQ_OP_SEQ_SCRIPTCTR:
                            write << "ASEQ_OP_SEQ_SCRIPTCTR,";
                            break;
                        case ASEQ_OP_SEQ_STOP:
                            write << "ASEQ_OP_SEQ_STOP,";
                            break;
                        case ASEQ_OP_SEQ_STSEQ:
                            write << "ASEQ_OP_SEQ_STSEQ,";
                            break;
                        case ASEQ_OP_SEQ_SUB:
                            write << "ASEQ_OP_SEQ_SUB,";
                            break;
                        case ASEQ_OP_SEQ_AND:
                            write << "ASEQ_OP_SEQ_AND,";
                            break;
                        case ASEQ_OP_SEQ_LDI:
                            write << "ASEQ_OP_SEQ_LDI,";
                            break;
                        case ASEQ_OP_SEQ_DYNCALL:
                            write << "ASEQ_OP_SEQ_DYNCALL,";
                            break;
                        case ASEQ_OP_SEQ_RAND:
                            write << "ASEQ_OP_SEQ_RAND,";
                            break;
                        case ASEQ_OP_SEQ_NOTEALLOC:
                            write << "ASEQ_OP_SEQ_NOTEALLOC,";
                            break;
                        case ASEQ_OP_SEQ_LDSHORTGATEARR:
                            write << "ASEQ_OP_SEQ_LDSHORTGATEARR,";
                            break;
                        case ASEQ_OP_SEQ_LDSHORTVELARR:
                            write << "ASEQ_OP_SEQ_LDSHORTVELARR,";
                            break;
                        case ASEQ_OP_SEQ_MUTEBHV:
                            write << "ASEQ_OP_SEQ_MUTEBHV,";
                            break;
                        case ASEQ_OP_SEQ_MUTE:
                            write << "ASEQ_OP_SEQ_MUTE,";
                            break;
                        case ASEQ_OP_SEQ_MUTESCALE:
                            write << "ASEQ_OP_SEQ_MUTESCALE,";
                            break;
                        case ASEQ_OP_SEQ_FREECHAN:
                            write << "ASEQ_OP_SEQ_FREECHAN,";
                            break;
                        case ASEQ_OP_SEQ_INITCHAN:
                            write << "ASEQ_OP_SEQ_INITCHAN,";
                            break;
                        case ASEQ_OP_SEQ_VOLSCALE:
                            write << "ASEQ_OP_SEQ_VOLSCALE,";
                            break;
                        case ASEQ_OP_SEQ_VOLMODE:
                            write << "ASEQ_OP_SEQ_VOLMODE,";
                            break;
                        case ASEQ_OP_SEQ_VOL:
                            write << "ASEQ_OP_SEQ_VOL,";
                            break;
                        case ASEQ_OP_SEQ_TEMPOCHG:
                            write << "ASEQ_OP_SEQ_TEMPOCHG,";
                            break;
                        case ASEQ_OP_SEQ_TEMPO:
                            write << "ASEQ_OP_SEQ_TEMPO,";
                            break;
                        case ASEQ_OP_SEQ_RTRANSPOSE:
                            write << "ASEQ_OP_SEQ_RTRANSPOSE,";
                            break;
                        case ASEQ_OP_SEQ_TRANSPOSE:
                            write << "ASEQ_OP_SEQ_TRANSPOSE,";
                            break;
                        case ASEQ_OP_SEQ_EF:
                            write << "ASEQ_OP_SEQ_EF,";
                            break;
                        case ASEQ_OP_SEQ_FREENOTELIST:
                            write << "ASEQ_OP_SEQ_FREENOTELIST,";
                            break;
                        case ASEQ_OP_SEQ_ALLOCNOTELIST:
                            write << "ASEQ_OP_SEQ_ALLOCNOTELIST,";
                            break;
                    }
                } else {
                    switch (command.cmd & 0xF0) {
                        case ASEQ_OP_SEQ_TESTCHAN:
                            write << "(ASEQ_OP_SEQ_TESTCHAN | ";
                            break;
                        case ASEQ_OP_SEQ_STOPCHAN:
                            write << "(ASEQ_OP_SEQ_STOPCHAN | ";
                            break;
                        case ASEQ_OP_SEQ_SUBIO:
                            write << "(ASEQ_OP_SEQ_SUBIO | ";
                            break;
                        case ASEQ_OP_SEQ_LDRES:
                            write << "(ASEQ_OP_SEQ_LDRES | ";
                            break;
                        case ASEQ_OP_SEQ_STIO:
                            write << "(ASEQ_OP_SEQ_STIO | ";
                            break;
                        case ASEQ_OP_SEQ_LDIO:
                            write << "(ASEQ_OP_SEQ_LDIO | ";
                            break;
                        case ASEQ_OP_SEQ_LDCHAN:
                            write << "(ASEQ_OP_SEQ_LDCHAN | ";
                            break;
                        case ASEQ_OP_SEQ_RLDCHAN:
                            write << "(ASEQ_OP_SEQ_RLDCHAN | ";
                            break;
                        case ASEQ_OP_SEQ_LDSEQ:
                            write << "(ASEQ_OP_SEQ_LDSEQ | ";
                            break;
                    }
                    write << FORMAT_HEX((command.cmd & 0xF), 1) << "),";
                }
            } else if (command.state == SequenceState::channel) {
                if (command.cmd >= 0xB0) {
                    switch (command.cmd) {
                        case ASEQ_OP_CHAN_LDFILTER:
                            write << "ASEQ_OP_CHAN_LDFILTER,";
                            break;
                        case ASEQ_OP_CHAN_FREEFILTER:
                            write << "ASEQ_OP_CHAN_FREEFILTER,";
                            break;
                        case ASEQ_OP_CHAN_LDSEQTOPTR:
                            write << "ASEQ_OP_CHAN_LDSEQTOPTR,";
                            break;
                        case ASEQ_OP_CHAN_FILTER:
                            write << "ASEQ_OP_CHAN_FILTER,";
                            break;
                        case ASEQ_OP_CHAN_PTRTODYNTBL:
                            write << "ASEQ_OP_CHAN_PTRTODYNTBL,";
                            break;
                        case ASEQ_OP_CHAN_DYNTBLTOPTR:
                            write << "ASEQ_OP_CHAN_DYNTBLTOPTR,";
                            break;
                        case ASEQ_OP_CHAN_DYNTBLV:
                            write << "ASEQ_OP_CHAN_DYNTBLV,";
                            break;
                        case ASEQ_OP_CHAN_RANDTOPTR:
                            write << "ASEQ_OP_CHAN_RANDTOPTR,";
                            break;
                        case ASEQ_OP_CHAN_RAND:
                            write << "ASEQ_OP_CHAN_RAND,";
                            break;
                        case ASEQ_OP_CHAN_RANDVEL:
                            write << "ASEQ_OP_CHAN_RANDVEL,";
                            break;
                        case ASEQ_OP_CHAN_RANDGATE:
                            write << "ASEQ_OP_CHAN_RANDGATE,";
                            break;
                        case ASEQ_OP_CHAN_COMBFILTER:
                            write << "ASEQ_OP_CHAN_COMBFILTER,";
                            break;
                        case ASEQ_OP_CHAN_PTRADD:
                            write << "ASEQ_OP_CHAN_PTRADD,";
                            break;
                        case ASEQ_OP_CHAN_SAMPLESTART:
                            write << "ASEQ_OP_CHAN_SAMPLESTART,";
                            break;
                        case ASEQ_OP_CHAN_INSTR:
                            write << "ASEQ_OP_CHAN_INSTR,";
                            break;
                        case ASEQ_OP_CHAN_DYNTBL:
                            write << "ASEQ_OP_CHAN_DYNTBL,";
                            break;
                        case ASEQ_OP_CHAN_SHORT:
                            write << "ASEQ_OP_CHAN_SHORT,";
                            break;
                        case ASEQ_OP_CHAN_NOSHORT:
                            write << "ASEQ_OP_CHAN_NOSHORT,";
                            break;
                        case ASEQ_OP_CHAN_DYNTBLLOOKUP:
                            write << "ASEQ_OP_CHAN_DYNTBLLOOKUP,";
                            break;
                        case ASEQ_OP_CHAN_FONT:
                            write << "ASEQ_OP_CHAN_FONT,";
                            break;
                        case ASEQ_OP_CHAN_STSEQ:
                            write << "ASEQ_OP_CHAN_STSEQ,";
                            break;
                        case ASEQ_OP_CHAN_SUB:
                            write << "ASEQ_OP_CHAN_SUB,";
                            break;
                        case ASEQ_OP_CHAN_AND:
                            write << "ASEQ_OP_CHAN_AND,";
                            break;
                        case ASEQ_OP_CHAN_MUTEBHV:
                            write << "ASEQ_OP_CHAN_MUTEBHV,";
                            break;
                        case ASEQ_OP_CHAN_LDSEQ:
                            write << "ASEQ_OP_CHAN_LDSEQ,";
                            break;
                        case ASEQ_OP_CHAN_LDI:
                            write << "ASEQ_OP_CHAN_LDI,";
                            break;
                        case ASEQ_OP_CHAN_STOPCHAN:
                            write << "ASEQ_OP_CHAN_STOPCHAN,";
                            break;
                        case ASEQ_OP_CHAN_LDPTR:
                            write << "ASEQ_OP_CHAN_LDPTR,";
                            break;
                        case ASEQ_OP_CHAN_STPTRTOSEQ:
                            write << "ASEQ_OP_CHAN_STPTRTOSEQ,";
                            break;
                        case ASEQ_OP_CHAN_EFFECTS:
                            write << "ASEQ_OP_CHAN_EFFECTS,";
                            break;
                        case ASEQ_OP_CHAN_NOTEALLOC:
                            write << "ASEQ_OP_CHAN_NOTEALLOC,";
                            break;
                        case ASEQ_OP_CHAN_SUSTAIN:
                            write << "ASEQ_OP_CHAN_SUSTAIN,";
                            break;
                        case ASEQ_OP_CHAN_BEND:
                            write << "ASEQ_OP_CHAN_BEND,";
                            break;
                        case ASEQ_OP_CHAN_REVERB:
                            write << "ASEQ_OP_CHAN_REVERB,";
                            break;
                        case ASEQ_OP_CHAN_VIBFREQ:
                            write << "ASEQ_OP_CHAN_VIBFREQ,";
                            break;
                        case ASEQ_OP_CHAN_VIBDEPTH:
                            write << "ASEQ_OP_CHAN_VIBDEPTH,";
                            break;
                        case ASEQ_OP_CHAN_RELEASERATE:
                            write << "ASEQ_OP_CHAN_RELEASERATE,";
                            break;
                        case ASEQ_OP_CHAN_ENV:
                            write << "ASEQ_OP_CHAN_ENV,";
                            break;
                        case ASEQ_OP_CHAN_TRANSPOSE:
                            write << "ASEQ_OP_CHAN_TRANSPOSE,";
                            break;
                        case ASEQ_OP_CHAN_PANWEIGHT:
                            write << "ASEQ_OP_CHAN_PANWEIGHT,";
                            break;
                        case ASEQ_OP_CHAN_PAN:
                            write << "ASEQ_OP_CHAN_PAN,";
                            break;
                        case ASEQ_OP_CHAN_FREQSCALE:
                            write << "ASEQ_OP_CHAN_FREQSCALE,";
                            break;
                        case ASEQ_OP_CHAN_VOL:
                            write << "ASEQ_OP_CHAN_VOL,";
                            break;
                        case ASEQ_OP_CHAN_VOLEXP:
                            write << "ASEQ_OP_CHAN_VOLEXP,";
                            break;
                        case ASEQ_OP_CHAN_VIBFREQGRAD:
                            write << "ASEQ_OP_CHAN_VIBFREQGRAD,";
                            break;
                        case ASEQ_OP_CHAN_VIBDEPTHGRAD:
                            write << "ASEQ_OP_CHAN_VIBDEPTHGRAD,";
                            break;
                        case ASEQ_OP_CHAN_VIBDELAY:
                            write << "ASEQ_OP_CHAN_VIBDELAY,";
                            break;
                        case ASEQ_OP_CHAN_DYNCALL:
                            write << "ASEQ_OP_CHAN_DYNCALL,";
                            break;
                        case ASEQ_OP_CHAN_REVERBIDX:
                            write << "ASEQ_OP_CHAN_REVERBIDX,";
                            break;
                        case ASEQ_OP_CHAN_SAMPLEBOOK:
                            write << "ASEQ_OP_CHAN_SAMPLEBOOK,";
                            break;
                        case ASEQ_OP_CHAN_LDPARAMS:
                            write << "ASEQ_OP_CHAN_LDPARAMS,";
                            break;
                        case ASEQ_OP_CHAN_PARAMS:
                            write << "ASEQ_OP_CHAN_PARAMS,";
                            break;
                        case ASEQ_OP_CHAN_NOTEPRI:
                            write << "ASEQ_OP_CHAN_NOTEPRI,";
                            break;
                        case ASEQ_OP_CHAN_STOP:
                            write << "ASEQ_OP_CHAN_STOP,";
                            break;
                        case ASEQ_OP_CHAN_FONTINSTR:
                            write << "ASEQ_OP_CHAN_FONTINSTR,";
                            break;
                        case ASEQ_OP_CHAN_VIBRESET:
                            write << "ASEQ_OP_CHAN_VIBRESET,";
                            break;
                        case ASEQ_OP_CHAN_GAIN:
                            write << "ASEQ_OP_CHAN_GAIN,";
                            break;
                        case ASEQ_OP_CHAN_BENDFINE:
                            write << "ASEQ_OP_CHAN_BENDFINE,";
                            break;
                        case ASEQ_OP_CHAN_EF:
                            write << "ASEQ_OP_CHAN_EF,";
                            break;
                        case ASEQ_OP_CHAN_FREENOTELIST:
                            write << "ASEQ_OP_CHAN_FREENOTELIST,";
                            break;
                        case ASEQ_OP_CHAN_ALLOCNOTELIST:
                            write << "ASEQ_OP_CHAN_ALLOCNOTELIST,";
                            break;
                    }
                } else if (command.cmd >= 0x78) {
                    switch (command.cmd & 0xF8) {
                        case ASEQ_OP_CHAN_RLDLAYER:
                            write << "(ASEQ_OP_CHAN_RLDLAYER | ";
                            break;
                        case ASEQ_OP_CHAN_TESTLAYER:
                            write << "(ASEQ_OP_CHAN_TESTLAYER | ";
                            break;
                        case ASEQ_OP_CHAN_LDLAYER:
                            write << "(ASEQ_OP_CHAN_LDLAYER | ";
                            break;
                        case ASEQ_OP_CHAN_DELLAYER:
                            write << "(ASEQ_OP_CHAN_DELLAYER | ";
                            break;
                        case ASEQ_OP_CHAN_DYNLDLAYER:
                            write << "(ASEQ_OP_CHAN_DYNLDLAYER | ";
                            break;
                    }
                    write << FORMAT_HEX((command.cmd & 0x7), 1) << "),";
                } else {
                    switch (command.cmd & 0xF0) {
                        case ASEQ_OP_CHAN_CDELAY:
                            write << "(ASEQ_OP_CHAN_CDELAY | ";
                            break;
                        case ASEQ_OP_CHAN_LDSAMPLE:
                            write << "(ASEQ_OP_CHAN_LDSAMPLE | ";
                            break;
                        case ASEQ_OP_CHAN_LDCHAN:
                            write << "(ASEQ_OP_CHAN_LDCHAN | ";
                            break;
                        case ASEQ_OP_CHAN_STCIO:
                            write << "(ASEQ_OP_CHAN_STCIO | ";
                            break;
                        case ASEQ_OP_CHAN_LDCIO:
                            write << "(ASEQ_OP_CHAN_LDCIO | ";
                            break;
                        case ASEQ_OP_CHAN_SUBIO:
                            write << "(ASEQ_OP_CHAN_SUBIO | ";
                            break;
                        case ASEQ_OP_CHAN_LDIO:
                            write << "(ASEQ_OP_CHAN_LDIO | ";
                            break;
                        case ASEQ_OP_CHAN_STIO:
                            write << "(ASEQ_OP_CHAN_STIO | ";
                            break;
                    }
                    write << FORMAT_HEX((command.cmd & 0xF), 1) << "),";
                }
            } else if (command.state == SequenceState::layer) {
                if (command.cmd > 0xC0) {
                    switch (command.cmd) {
                        case ASEQ_OP_LAYER_SHORTVEL:
                            write << "ASEQ_OP_LAYER_SHORTVEL,";
                            break;
                        case ASEQ_OP_LAYER_TRANSPOSE:
                            write << "ASEQ_OP_LAYER_TRANSPOSE,";
                            break;
                        case ASEQ_OP_LAYER_SHORTDELAY:
                            write << "ASEQ_OP_LAYER_SHORTDELAY,";
                            break;
                        case ASEQ_OP_LAYER_LEGATO:
                            write << "ASEQ_OP_LAYER_LEGATO,";
                            break;
                        case ASEQ_OP_LAYER_NOLEGATO:
                            write << "ASEQ_OP_LAYER_NOLEGATO,";
                            break;
                        case ASEQ_OP_LAYER_INSTR:
                            write << "ASEQ_OP_LAYER_INSTR,";
                            break;
                        case ASEQ_OP_LAYER_PORTAMENTO:
                            write << "ASEQ_OP_LAYER_PORTAMENTO,";
                            break;
                        case ASEQ_OP_LAYER_NOPORTAMENTO:
                            write << "ASEQ_OP_LAYER_NOPORTAMENTO,";
                            break;
                        case ASEQ_OP_LAYER_SHORTGATE:
                            write << "ASEQ_OP_LAYER_SHORTGATE,";
                            break;
                        case ASEQ_OP_LAYER_NOTEPAN:
                            write << "ASEQ_OP_LAYER_NOTEPAN,";
                            break;
                        case ASEQ_OP_LAYER_ENV:
                            write << "ASEQ_OP_LAYER_ENV,";
                            break;
                        case ASEQ_OP_LAYER_NODRUMPAN:
                            write << "ASEQ_OP_LAYER_NODRUMPAN,";
                            break;
                        case ASEQ_OP_LAYER_STEREO:
                            write << "ASEQ_OP_LAYER_STEREO,";
                            break;
                        case ASEQ_OP_LAYER_BENDFINE:
                            write << "ASEQ_OP_LAYER_BENDFINE,";
                            break;
                        case ASEQ_OP_LAYER_RELEASERATE:
                            write << "ASEQ_OP_LAYER_RELEASERATE,";
                            break;
                        default:
                            switch (command.cmd & 0xF0) {
                                case ASEQ_OP_LAYER_LDSHORTVEL:
                                    write << "(ASEQ_OP_LAYER_LDSHORTVEL | ";
                                    write << FORMAT_HEX((command.cmd & 0xF), 1) << "),";
                                    break;
                                case ASEQ_OP_LAYER_LDSHORTGATE:
                                    write << "(ASEQ_OP_LAYER_LDSHORTGATE | ";
                                    write << FORMAT_HEX((command.cmd & 0xF), 1) << "),";
                                    break;
                            }
                            break;
                    }
                } else {
                    if (command.cmd == ASEQ_OP_LAYER_LDELAY) {
                        write << "ASEQ_OP_LAYER_LDELAY,";
                    } else {
                        switch (command.cmd & 0xC0) {
                            case ASEQ_OP_LAYER_NOTEDVG:
                                write << "(ASEQ_OP_LAYER_NOTEDVG | ";
                                break;
                            case ASEQ_OP_LAYER_NOTEDV:
                                write << "(ASEQ_OP_LAYER_NOTEDV | ";
                                break;
                            case ASEQ_OP_LAYER_NOTEVG:
                                write << "(ASEQ_OP_LAYER_NOTEVG | ";
                                break;
                        }

                        switch (command.cmd - (command.cmd & 0xC0)) {
                            case PITCH_A0:
                                write << "PITCH_A0),";
                                break;
                            case PITCH_BF0:
                                write << "PITCH_BF0),";
                                break;
                            case PITCH_B0:
                                write << "PITCH_B0),";
                                break;
                            case PITCH_C1:
                                write << "PITCH_C1),";
                                break;
                            case PITCH_DF1:
                                write << "PITCH_DF1),";
                                break;
                            case PITCH_D1:
                                write << "PITCH_D1),";
                                break;
                            case PITCH_EF1:
                                write << "PITCH_EF1),";
                                break;
                            case PITCH_E1:
                                write << "PITCH_E1),";
                                break;
                            case PITCH_F1:
                                write << "PITCH_F1),";
                                break;
                            case PITCH_GF1:
                                write << "PITCH_GF1),";
                                break;
                            case PITCH_G1:
                                write << "PITCH_G1),";
                                break;
                            case PITCH_AF1:
                                write << "PITCH_AF1),";
                                break;
                            case PITCH_A1:
                                write << "PITCH_A1),";
                                break;
                            case PITCH_BF1:
                                write << "PITCH_BF1),";
                                break;
                            case PITCH_B1:
                                write << "PITCH_B1),";
                                break;
                            case PITCH_C2:
                                write << "PITCH_C2),";
                                break;
                            case PITCH_DF2:
                                write << "PITCH_DF2),";
                                break;
                            case PITCH_D2:
                                write << "PITCH_D2),";
                                break;
                            case PITCH_EF2:
                                write << "PITCH_EF2),";
                                break;
                            case PITCH_E2:
                                write << "PITCH_E2),";
                                break;
                            case PITCH_F2:
                                write << "PITCH_F2),";
                                break;
                            case PITCH_GF2:
                                write << "PITCH_GF2),";
                                break;
                            case PITCH_G2:
                                write << "PITCH_G2),";
                                break;
                            case PITCH_AF2:
                                write << "PITCH_AF2),";
                                break;
                            case PITCH_A2:
                                write << "PITCH_A2),";
                                break;
                            case PITCH_BF2:
                                write << "PITCH_BF2),";
                                break;
                            case PITCH_B2:
                                write << "PITCH_B2),";
                                break;
                            case PITCH_C3:
                                write << "PITCH_C3),";
                                break;
                            case PITCH_DF3:
                                write << "PITCH_DF3),";
                                break;
                            case PITCH_D3:
                                write << "PITCH_D3),";
                                break;
                            case PITCH_EF3:
                                write << "PITCH_EF3),";
                                break;
                            case PITCH_E3:
                                write << "PITCH_E3),";
                                break;
                            case PITCH_F3:
                                write << "PITCH_F3),";
                                break;
                            case PITCH_GF3:
                                write << "PITCH_GF3),";
                                break;
                            case PITCH_G3:
                                write << "PITCH_G3),";
                                break;
                            case PITCH_AF3:
                                write << "PITCH_AF3),";
                                break;
                            case PITCH_A3:
                                write << "PITCH_A3),";
                                break;
                            case PITCH_BF3:
                                write << "PITCH_BF3),";
                                break;
                            case PITCH_B3:
                                write << "PITCH_B3),";
                                break;
                            case PITCH_C4:
                                write << "PITCH_C4),";
                                break;
                            case PITCH_DF4:
                                write << "PITCH_DF4),";
                                break;
                            case PITCH_D4:
                                write << "PITCH_D4),";
                                break;
                            case PITCH_EF4:
                                write << "PITCH_EF4),";
                                break;
                            case PITCH_E4:
                                write << "PITCH_E4),";
                                break;
                            case PITCH_F4:
                                write << "PITCH_F4),";
                                break;
                            case PITCH_GF4:
                                write << "PITCH_GF4),";
                                break;
                            case PITCH_G4:
                                write << "PITCH_G4),";
                                break;
                            case PITCH_AF4:
                                write << "PITCH_AF4),";
                                break;
                            case PITCH_A4:
                                write << "PITCH_A4),";
                                break;
                            case PITCH_BF4:
                                write << "PITCH_BF4),";
                                break;
                            case PITCH_B4:
                                write << "PITCH_B4),";
                                break;
                            case PITCH_C5:
                                write << "PITCH_C5),";
                                break;
                            case PITCH_DF5:
                                write << "PITCH_DF5),";
                                break;
                            case PITCH_D5:
                                write << "PITCH_D5),";
                                break;
                            case PITCH_EF5:
                                write << "PITCH_EF5),";
                                break;
                            case PITCH_E5:
                                write << "PITCH_E5),";
                                break;
                            case PITCH_F5:
                                write << "PITCH_F5),";
                                break;
                            case PITCH_GF5:
                                write << "PITCH_GF5),";
                                break;
                            case PITCH_G5:
                                write << "PITCH_G5),";
                                break;
                            case PITCH_AF5:
                                write << "PITCH_AF5),";
                                break;
                            case PITCH_A5:
                                write << "PITCH_A5),";
                                break;
                            case PITCH_BF5:
                                write << "PITCH_BF5),";
                                break;
                            case PITCH_B5:
                                write << "PITCH_B5),";
                                break;
                            case PITCH_C6:
                                write << "PITCH_C6),";
                                break;
                            case PITCH_DF6:
                                write << "PITCH_DF6),";
                                break;
                            case PITCH_D6:
                                write << "PITCH_D6),";
                                break;
                            case PITCH_EF6:
                                write << "PITCH_EF6),";
                                break;
                            case PITCH_E6:
                                write << "PITCH_E6),";
                                break;
                            case PITCH_F6:
                                write << "PITCH_F6),";
                                break;
                            case PITCH_GF6:
                                write << "PITCH_GF6),";
                                break;
                            case PITCH_G6:
                                write << "PITCH_G6),";
                                break;
                            case PITCH_AF6:
                                write << "PITCH_AF6),";
                                break;
                            case PITCH_A6:
                                write << "PITCH_A6),";
                                break;
                            case PITCH_BF6:
                                write << "PITCH_BF6),";
                                break;
                            case PITCH_B6:
                                write << "PITCH_B6),";
                                break;
                            case PITCH_C7:
                                write << "PITCH_C7),";
                                break;
                            case PITCH_DF7:
                                write << "PITCH_DF7),";
                                break;
                            case PITCH_D7:
                                write << "PITCH_D7),";
                                break;
                            case PITCH_EF7:
                                write << "PITCH_EF7),";
                                break;
                            case PITCH_E7:
                                write << "PITCH_E7),";
                                break;
                            case PITCH_F7:
                                write << "PITCH_F7),";
                                break;
                            case PITCH_GF7:
                                write << "PITCH_GF7),";
                                break;
                            case PITCH_G7:
                                write << "PITCH_G7),";
                                break;
                            case PITCH_AF7:
                                write << "PITCH_AF7),";
                                break;
                            case PITCH_A7:
                                write << "PITCH_A7),";
                                break;
                            case PITCH_BF7:
                                write << "PITCH_BF7),";
                                break;
                            case PITCH_B7:
                                write << "PITCH_B7),";
                                break;
                            case PITCH_C8:
                                write << "PITCH_C8),";
                                break;
                            case PITCH_DF8:
                                write << "PITCH_DF8),";
                                break;
                            case PITCH_D8:
                                write << "PITCH_D8),";
                                break;
                            case PITCH_EF8:
                                write << "PITCH_EF8),";
                                break;
                            case PITCH_E8:
                                write << "PITCH_E8),";
                                break;
                            case PITCH_F8:
                                write << "PITCH_F8),";
                                break;
                            case PITCH_GF8:
                                write << "PITCH_GF8),";
                                break;
                            case PITCH_G8:
                                write << "PITCH_G8),";
                                break;
                            case PITCH_AF8:
                                write << "PITCH_AF8),";
                                break;
                            case PITCH_A8:
                                write << "PITCH_A8),";
                                break;
                            case PITCH_BF8:
                                write << "PITCH_BF8),";
                                break;
                            case PITCH_B8:
                                write << "PITCH_B8),";
                                break;
                            case PITCH_C9:
                                write << "PITCH_C9),";
                                break;
                            case PITCH_DF9:
                                write << "PITCH_DF9),";
                                break;
                            case PITCH_D9:
                                write << "PITCH_D9),";
                                break;
                            case PITCH_EF9:
                                write << "PITCH_EF9),";
                                break;
                            case PITCH_E9:
                                write << "PITCH_E9),";
                                break;
                            case PITCH_F9:
                                write << "PITCH_F9),";
                                break;
                            case PITCH_GF9:
                                write << "PITCH_GF9),";
                                break;
                            case PITCH_G9:
                                write << "PITCH_G9),";
                                break;
                            case PITCH_AF9:
                                write << "PITCH_AF9),";
                                break;
                            case PITCH_A9:
                                write << "PITCH_A9),";
                                break;
                            case PITCH_BF9:
                                write << "PITCH_BF9),";
                                break;
                            case PITCH_B9:
                                write << "PITCH_B9),";
                                break;
                            case PITCH_C10:
                                write << "PITCH_C10),";
                                break;
                            case PITCH_DF10:
                                write << "PITCH_DF10),";
                                break;
                            case PITCH_D10:
                                write << "PITCH_D10),";
                                break;
                            case PITCH_EF10:
                                write << "PITCH_EF10),";
                                break;
                            case PITCH_E10:
                                write << "PITCH_E10),";
                                break;
                            case PITCH_F10:
                                write << "PITCH_F10),";
                                break;
                            case PITCH_BFNEG1:
                                write << "PITCH_BFNEG1),";
                                break;
                            case PITCH_BNEG1:
                                write << "PITCH_BNEG1),";
                                break;
                            case PITCH_C0:
                                write << "PITCH_C0),";
                                break;
                            case PITCH_DF0:
                                write << "PITCH_DF0),";
                                break;
                            case PITCH_D0:
                                write << "PITCH_D0),";
                                break;
                            case PITCH_EF0:
                                write << "PITCH_EF0),";
                                break;
                            case PITCH_E0:
                                write << "PITCH_E0),";
                                break;
                            case PITCH_F0:
                                write << "PITCH_F0),";
                                break;
                            case PITCH_GF0:
                                write << "PITCH_GF0),";
                                break;
                            case PITCH_G0:
                                write << "PITCH_G0),";
                                break;
                            case PITCH_AF0:
                                write << "PITCH_AF0),";
                                break;
                        }
                    }
                }
            }
        }

        for (const auto& arg : command.args) {
            write << " ";
            switch(static_cast<SeqArgType>(arg.index())) {
                case SeqArgType::U8:
                    // if (command.cmd == ASEQ_OP_RJUMP || command.cmd == ASEQ_OP_RBLTZ || command.cmd == ASEQ_OP_RBEQZ) {
                    //     int32_t temp = static_cast<int32_t>((int8_t)(std::get<uint8_t>(arg) & 0xFF));
                    //     if (temp >= 0) {
                    //         write << FORMAT_HEX(temp, 2) << ",";
                    //     } else {
                    //         write << "-" << FORMAT_HEX((0x100 - (temp & 0xFF)), 2) << ",";
                    //     }
                    // } else {
                    write << FORMAT_HEX(static_cast<uint32_t>(std::get<uint8_t>(arg)), 2) << ",";
                    // }
                    break;
                case SeqArgType::S16:
                    write << "S16(" << FORMAT_HEX(std::get<uint16_t>(arg), 4) << "),";
                    break;
                case SeqArgType::CS16:
                    write << "CS16(" << FORMAT_HEX(std::get<uint32_t>(arg), 4) << "),";
                    break;
            }
        }

        if (command.cmd == ASEQ_OP_RJUMP || command.cmd == ASEQ_OP_RBLTZ || command.cmd == ASEQ_OP_RBEQZ) {
            write << " /* LABEL: L____" << FORMAT_HEX2(command.pos + static_cast<int32_t>((int8_t)(std::get<uint8_t>(command.args.at(0)) & 0xFF)) + 2, 3) << " */";
        } else if (command.cmd == ASEQ_OP_JUMP || command.cmd == ASEQ_OP_BLTZ || command.cmd == ASEQ_OP_BEQZ || ((command.cmd & 0xF0) == ASEQ_OP_SEQ_LDCHAN && command.state == SequenceState::player) || ((command.cmd & 0xF8) == ASEQ_OP_CHAN_LDLAYER && command.state == SequenceState::channel)) {
            write << " /* LABEL: L____" << FORMAT_HEX2(std::get<uint16_t>(command.args.at(0)), 3) << " */";
        }

        write << "\n";
    }

    write << "};\n\n";

    return offset;
}

ExportResult FZX::SequenceBinaryExporter::Export(std::ostream &write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node &node, std::string* replacement ) {
    // Nothing Required Here For Binary Exporting

    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> FZX::SequenceFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    const auto offset = GetSafeNode<uint32_t>(node, "offset");
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto size = GetSafeNode<uint32_t>(node, "size");
    LUS::BinaryReader reader(segment.data, segment.size);
    uint32_t temp;

    reader.SetEndianness(Torch::Endianness::Big);

    std::vector<SeqCommand> buf;

    uint32_t count = 0;
    SequenceState state = SequenceState::player;
    bool largeNotes = false;
    int32_t channel = -1;
    int32_t layer = -1;
    std::deque<SeqLabelInfo> posStack;
    std::deque<uint16_t> dynTableStack;
    std::unordered_set<uint32_t> existingPositions;
    uint32_t depth = 0;
    std::unordered_set<uint32_t> labels;

    while (count < size || !posStack.empty()) {
        SeqCommand command;
        bool nextLabel = false;
        command.state = state;
        command.pos = count;
        command.channel = channel;
        command.layer = layer;

        if (!existingPositions.contains(command.pos) && command.pos < size) {
            existingPositions.insert(command.pos);
        } else {
            while (!posStack.empty() && existingPositions.contains(posStack.back().pos)) {
                SPDLOG_ERROR("POP BACK 0x{:X}", posStack.back().pos);
                posStack.pop_back();
            }
            if (posStack.empty()) {
                SPDLOG_ERROR("BREAK 0x{:X} 0x{:X}", count, command.cmd);
                break;
            }

            SeqLabelInfo& labelInfo = posStack.back();

            command.state = state = labelInfo.state;
            command.pos = count = labelInfo.pos;
            command.channel = channel = labelInfo.channel;
            command.layer = layer = labelInfo.layer;
            largeNotes = labelInfo.largeNotes;
            SPDLOG_ERROR("POP BACK 0x{:X}", posStack.back().pos);
            posStack.pop_back();
            existingPositions.insert(command.pos);
        }

        uint32_t startPos = command.pos;
        reader.Seek(command.pos, LUS::SeekOffsetType::Start);

        if (command.state == SequenceState::data) {
            reader.Seek(command.pos, LUS::SeekOffsetType::Start);
            uint16_t dataValue = 0;
            while (true) {
                dataValue = READ_S16;

                if (!(dataValue >= 0 && dataValue < size && count < size)) {
                    break;
                }

                SequenceState dataState;
                if (command.channel == -1) {
                    dataState = SequenceState::player;
                } else if (command.layer == -1) {
                    dataState = SequenceState::channel;
                } else {
                    dataState = SequenceState::layer;
                }
                posStack.emplace_back(dataValue, dataState, command.channel, command.layer, largeNotes);
                command.args.push_back(dataValue);
                labels.insert(dataValue);
            }
            command.size = count - startPos - sizeof(uint16_t);
            buf.push_back(command);

            if (posStack.empty()) {
                SPDLOG_ERROR("BREAK 0x{:X} 0x{:X}", count, command.cmd);
                break;
            }

            SeqLabelInfo& labelInfo = posStack.back();

            state = labelInfo.state;
            count = labelInfo.pos;
            channel = labelInfo.channel;
            layer = labelInfo.layer;
            largeNotes = labelInfo.largeNotes;
            SPDLOG_ERROR("POP BACK 0x{:X}", posStack.back().pos);
            posStack.pop_back();
            continue;
        }

        command.cmd = READ_U8;

        SPDLOG_ERROR("POS: 0x{:X}, CMD: 0x{:X}", command.pos, command.cmd);

        switch (command.cmd) {
            case ASEQ_OP_RBLTZ: {
                uint8_t jumpRPos = READ_U8;
                int8_t jumpRSignedPos = (int8_t) (jumpRPos & 0xFF);
                command.args.emplace_back(jumpRPos);
                posStack.emplace_back(count + jumpRSignedPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(count + jumpRSignedPos);
                break;
            }
            case ASEQ_OP_RBEQZ: {
                uint8_t jumpRPos = READ_U8;
                int8_t jumpRSignedPos = (int8_t) (jumpRPos & 0xFF);
                command.args.emplace_back(jumpRPos);
                posStack.emplace_back(count + jumpRSignedPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(count + jumpRSignedPos);
                break;
            }
            case ASEQ_OP_RJUMP: {
                uint8_t jumpRPos = READ_U8;
                int8_t jumpRSignedPos = (int8_t) (jumpRPos & 0xFF);
                command.args.emplace_back(jumpRPos);
                posStack.emplace_back(count + jumpRSignedPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(count + jumpRSignedPos);
                nextLabel = true;
                break;
            }
            case ASEQ_OP_BGEZ: {
                uint16_t jumpPos = READ_S16;
                command.args.emplace_back(jumpPos);
                posStack.emplace_back(jumpPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(jumpPos);
                break;
            }
            case ASEQ_OP_BREAK:
                depth--;
                break;
            case ASEQ_OP_LOOPEND:
                depth--;
                break;
            case ASEQ_OP_LOOP:
                command.args.emplace_back(READ_U8);
                depth++;
                break;
            case ASEQ_OP_BLTZ: {
                uint16_t jumpPos = READ_S16;
                command.args.emplace_back(jumpPos);
                posStack.emplace_back(jumpPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(jumpPos);
                break;
            }
            case ASEQ_OP_BEQZ: {
                uint16_t jumpPos = READ_S16;
                command.args.emplace_back(jumpPos);
                posStack.emplace_back(jumpPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(jumpPos);
                break;
            }
            case ASEQ_OP_JUMP: {
                uint16_t jumpPos = READ_S16;
                command.args.emplace_back(jumpPos);
                posStack.emplace_back(jumpPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(jumpPos);
                nextLabel = true;
                break;
            }
            case ASEQ_OP_CALL: {
                uint16_t jumpPos = READ_S16;
                command.args.emplace_back(jumpPos);
                posStack.emplace_back(jumpPos, command.state, command.channel, command.layer, largeNotes);
                labels.insert(jumpPos);
                depth++;
                break;
            }
            case ASEQ_OP_DELAY: {
                uint32_t delay = READ_CS16;
                command.args.emplace_back(delay);
                // nextLabel = delay != 0;
                break;
            }
            case ASEQ_OP_DELAY1:
                // nextLabel = true;
                break;
            case ASEQ_OP_END:
                if (depth == 0) {
                    // if (state == SequenceState::layer) {
                    //     // Hack to actually parse more (not representative of code)
                    //     state = SequenceState::channel;
                    //     layer = -1;
                    // } else {
                    // }
                    nextLabel = true;
                } else {
                    depth--;
                }
                break;
            default:
                break;
        }

        if (command.cmd < ASEQ_OP_CONTROL_FLOW_FIRST) {
            if (state == SequenceState::player) {
                if (command.cmd >= 0xC0) {
                    switch (command.cmd) {
                        case ASEQ_OP_SEQ_RUNSEQ:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_SCRIPTCTR:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_STSEQ:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_SUB:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_AND:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_LDI:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_DYNCALL:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_RAND:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_NOTEALLOC:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_LDSHORTGATEARR:
                        case ASEQ_OP_SEQ_LDSHORTVELARR:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_MUTEBHV:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_MUTESCALE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_FREECHAN:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_INITCHAN: {
                            uint16_t channelMap = READ_S16;
                            command.args.emplace_back(channelMap);
                            break;
                        }
                        case ASEQ_OP_SEQ_VOLSCALE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_VOLMODE:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_SEQ_VOL:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_TEMPOCHG:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_TEMPO:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_RTRANSPOSE:
                        case ASEQ_OP_SEQ_TRANSPOSE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_EF:
                            command.args.emplace_back(READ_S16);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_ALLOCNOTELIST:
                            command.args.emplace_back(READ_U8);
                            break;
                        default:
                            break;
                    }
                } else {
                    switch (command.cmd & 0xF0) {
                        case ASEQ_OP_SEQ_LDRES:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_SEQ_LDCHAN: {
                            uint16_t chanPos = READ_S16;
                            command.args.emplace_back(chanPos);
                            posStack.emplace_back(chanPos, SequenceState::channel, command.cmd & 0xF, command.layer, largeNotes);
                            labels.insert(chanPos);
                            SPDLOG_ERROR("PLAYER LDCHAN POS: 0x{:X}, Chan: {}", chanPos, command.cmd & 0xF);
                            break;
                        }
                        case ASEQ_OP_SEQ_RLDCHAN: {
                            uint16_t chanRPos = READ_S16;
                            command.args.emplace_back(chanRPos);
                            posStack.emplace_back(count + chanRPos, SequenceState::channel, command.cmd & 0xF, command.layer, largeNotes);
                            labels.insert(count + chanRPos);
                            SPDLOG_ERROR("PLAYER RLDCHAN POS: 0x{:X}, Chan: {}", count + chanRPos, command.cmd & 0xF);
                            break;
                        }
                        case ASEQ_OP_SEQ_LDSEQ:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_S16);
                            break;
                        default:
                            break;
                    }
                }
            } else if (state == SequenceState::channel) {
                if (command.cmd >= 0xB0) {
                    switch (command.cmd) {
                        case ASEQ_OP_CHAN_LDFILTER:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_FREEFILTER:
                            break;
                        case ASEQ_OP_CHAN_LDSEQTOPTR:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_FILTER:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_PTRTODYNTBL:
                            break;
                        case ASEQ_OP_CHAN_DYNTBLTOPTR:
                            break;
                        case ASEQ_OP_CHAN_DYNTBLV:
                            break;
                        case ASEQ_OP_CHAN_RANDTOPTR:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_RAND:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_RANDVEL:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_RANDGATE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_COMBFILTER:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_PTRADD:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_SAMPLESTART:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_INSTR:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_DYNTBL: {
                            uint16_t dynValue = READ_S16;
                            command.args.emplace_back(dynValue);
                            dynTableStack.push_back(dynValue);
                            break;
                        }
                        case ASEQ_OP_CHAN_SHORT:
                            largeNotes = false;
                            break;
                        case ASEQ_OP_CHAN_NOSHORT:
                            largeNotes = true;
                            break;
                        case ASEQ_OP_CHAN_DYNTBLLOOKUP:
                            break;
                        case ASEQ_OP_CHAN_FONT:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_STSEQ:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_SUB:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_AND:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_MUTEBHV:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_LDSEQ:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_LDI:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_STOPCHAN:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_LDPTR:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_STPTRTOSEQ:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_EFFECTS:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_NOTEALLOC:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_SUSTAIN:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_BEND:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_REVERB:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBFREQ:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBDEPTH:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_RELEASERATE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_ENV:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_TRANSPOSE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_PANWEIGHT:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_PAN:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_FREQSCALE:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_VOL:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VOLEXP:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBFREQGRAD:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBDEPTHGRAD:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBDELAY:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_DYNCALL:
                            posStack.emplace_back(dynTableStack.back(), SequenceState::data, command.channel, command.layer, largeNotes);
                            labels.insert(dynTableStack.back());
                            dynTableStack.pop_back();
                            break;
                        case ASEQ_OP_CHAN_REVERBIDX:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_SAMPLEBOOK:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_LDPARAMS:
                            command.args.emplace_back(READ_S16);
                            break;
                        case ASEQ_OP_CHAN_PARAMS:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_NOTEPRI:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_STOP:
                            nextLabel = true;
                            break;
                        case ASEQ_OP_CHAN_FONTINSTR:
                            command.args.emplace_back(READ_U8);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_VIBRESET:
                            break;
                        case ASEQ_OP_CHAN_GAIN:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_BENDFINE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_EF:
                            command.args.emplace_back(READ_S16);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_FREENOTELIST:
                            break;
                        case ASEQ_OP_CHAN_ALLOCNOTELIST:
                            command.args.emplace_back(READ_U8);
                            break;
                    }
                } else if (command.cmd >= 0x78) {
                    switch (command.cmd & 0xF8) {
                        case ASEQ_OP_CHAN_LDLAYER: {
                            uint16_t layerPos = READ_S16;
                            command.args.emplace_back(layerPos);
                            posStack.emplace_back(layerPos, SequenceState::layer, command.channel, command.cmd & 0x7, largeNotes);
                            labels.insert(layerPos);
                            break;
                        }
                        case ASEQ_OP_CHAN_RLDLAYER: {
                            uint16_t layerRPos = READ_S16;
                            command.args.emplace_back(layerRPos);
                            posStack.emplace_back(count + layerRPos, SequenceState::layer, command.channel, command.cmd & 0x7, largeNotes);
                            labels.insert(count + layerRPos);
                            break;
                        }
                        default:
                            break;
                    }
                } else {
                    switch (command.cmd & 0xF0) {
                        case ASEQ_OP_CHAN_CDELAY:
                            // nextLabel = true;
                            break;
                        case ASEQ_OP_CHAN_LDSAMPLE:
                            break;
                        case ASEQ_OP_CHAN_LDCHAN: {
                            uint16_t chanPos = READ_S16;
                            command.args.emplace_back(chanPos);
                            posStack.emplace_back(chanPos, SequenceState::channel, command.cmd & 0xF, command.layer, largeNotes);
                            labels.insert(chanPos);
                            break;
                        }
                        case ASEQ_OP_CHAN_STCIO:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_LDCIO:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_CHAN_SUBIO:
                            break;
                        case ASEQ_OP_CHAN_LDIO:
                            break;
                        case ASEQ_OP_CHAN_STIO:
                            break;
                    }
                }
            } else if (state == SequenceState::layer) {
                if (command.cmd > 0xC0) {
                    switch (command.cmd) {
                        case ASEQ_OP_LAYER_SHORTVEL:
                        case ASEQ_OP_LAYER_NOTEPAN:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_SHORTGATE:
                        case ASEQ_OP_LAYER_TRANSPOSE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_LEGATO:
                        case ASEQ_OP_LAYER_NOLEGATO:
                            break;
                        case ASEQ_OP_LAYER_SHORTDELAY:
                            command.args.emplace_back(READ_CS16);
                            break;
                        case ASEQ_OP_LAYER_INSTR:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_PORTAMENTO: {
                            uint8_t portamento = READ_U8;
                            command.args.emplace_back(portamento);
                            command.args.emplace_back(READ_U8);
                            if (portamento & 0x80) {
                                command.args.emplace_back(READ_U8);
                            } else {
                                command.args.emplace_back(READ_CS16);
                            }
                            break;
                        }
                        case ASEQ_OP_LAYER_NOPORTAMENTO:
                            break;
                        case ASEQ_OP_LAYER_ENV:
                            command.args.emplace_back(READ_S16);
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_RELEASERATE:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_NODRUMPAN:
                            break;
                        case ASEQ_OP_LAYER_STEREO:
                            command.args.emplace_back(READ_U8);
                            break;
                        case ASEQ_OP_LAYER_BENDFINE:
                            command.args.emplace_back(READ_U8);
                            break;
                    }
                } else {
                    if (command.cmd == ASEQ_OP_LAYER_LDELAY) {
                        command.args.emplace_back(READ_CS16);
                    } else {
                        switch (command.cmd & 0xC0) {
                            case ASEQ_OP_LAYER_NOTEDVG:
                                command.args.emplace_back(READ_CS16);
                                if (largeNotes) {
                                    command.args.emplace_back(READ_U8);
                                    command.args.emplace_back(READ_U8);
                                }
                                break;
                            case ASEQ_OP_LAYER_NOTEDV:
                                if (largeNotes) {
                                    command.args.emplace_back(READ_CS16);
                                    command.args.emplace_back(READ_U8);
                                }
                                break;
                            case ASEQ_OP_LAYER_NOTEVG:
                                if (largeNotes) {
                                    command.args.emplace_back(READ_U8);
                                    command.args.emplace_back(READ_U8);
                                }
                                break;
                        }
                    }
                    // There sometimes seems to be another instruction here?
                    // nextLabel = true;
                }
            }
        }

        command.size = count - startPos;
        buf.push_back(command);

        if (nextLabel) {
            if (posStack.empty()) {
                SPDLOG_ERROR("BREAK 0x{:X} 0x{:X}", count, command.cmd);
                break;
            }

            SeqLabelInfo& labelInfo = posStack.back();

            state = labelInfo.state;
            count = labelInfo.pos;
            channel = labelInfo.channel;
            layer = labelInfo.layer;
            largeNotes = labelInfo.largeNotes;
            SPDLOG_ERROR("POP BACK 0x{:X}", posStack.back().pos);
            posStack.pop_back();
            continue;
        }
    }

    return std::make_shared<SequenceData>(buf, labels);
}
