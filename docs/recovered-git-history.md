# Recovered Git History

This file documents the original commit history of the OoT asset support branch.
The original git history was lost due to filesystem damage and recovered via ext4magic.
Source code was recovered from raw filesystem blocks; this reflog was recovered from
`.git/logs/HEAD`.

## Original Reflog

```
0000000 -> ad23cfd  branch: Created from HEAD
ad23cfd -> f157a45  commit: Add OoT build support and resource type codes
f157a45 -> 3402daf  commit: Add XML-to-YAML conversion script for OoT assets
3402daf -> 0baa34c  commit: Add OoT Array factory for Shipwright-compatible VTX and Vec3s arrays
0baa34c -> 77f8e1f  commit: Fix DList export for OoT: G_SETTIMG encoding, VTX lookup, and tooling
77f8e1f -> c1055fa  commit: Add cross-segment DList handling and generate all object YAMLs
c1055fa -> 58b17b9  commit: Remove generated object YAMLs from repo
58b17b9 -> 77f8e1f  reset: moving to 77f8e1f
77f8e1f -> 4c21525  commit: Add cross-segment DList handling for OoT compatibility
4c21525 -> 6b497e2  commit: Fix DList export to 99.94% pass rate for OoT objects
6b497e2 -> d0c5e6d  commit: Fix remaining DList edge cases: cross-file VTX and segment 8-13 G_DL
d0c5e6d -> 7543acb  commit: Add test tooling: list_assets.py, test_assets.sh, and shared lib.sh
7543acb -> e7f90af  commit: Fix gSunDL: port OTRExporter workaround for ROM texture format bug
e7f90af -> 118c9bf  commit: Add THROW_ON_UNKNOWN_TYPE option and rewrite test_assets.sh for single Torch run
118c9bf -> 842fe82  commit: Add reference manifest for pal_gc
842fe82 -> 363e770  commit: Make manifest.sh accept O2R and output path arguments
363e770 -> 874e33d  commit: Speed up manifest.sh with batched sha256sum
874e33d -> 3a0c2fa  commit: Rewrite test_assets.sh to use manifest comparison instead of per-file hashing
3a0c2fa -> 16b38d6  commit: Fix MTX binary export to match OTRExporter output
16b38d6 -> ac087b1  commit: Merge xml_to_yaml and add_vtx_from_manifest into zapd_to_torch
ac087b1 -> 16b38d6  reset: moving to HEAD~1
16b38d6 -> 831f168  commit: Merge xml_to_yaml and add_vtx_from_manifest into zapd_to_torch
831f168 -> 557487c  commit: Add overlay virtual address support and cross-segment resolution
557487c -> 1e24859  commit: Fix remaining 4 overlay DList failures
1e24859 -> 0e79580  commit: Fix version file CRC byte order
0e79580 -> 68d818b  commit: Gitignore generated YAML asset definitions
68d818b -> 3ecba74  commit: Update pal_gc reference manifest from SOH c270a63
3ecba74 -> 05f3096  commit: Add OoT skeleton and limb factories
05f3096 -> d8c0289  commit: Use temp scratch dirs and parameterize ROM version
d8c0289 -> 26465ea  commit: Fix auto-discovered limb DList path generation
26465ea -> fdbe118  commit: Add OoT normal and curve animation factories
fdbe118 -> 9749269  commit: Add OoT player animation data factory
9749269 -> bb1b59d  commit: Add OoT collision factory
bb1b59d -> df662c8  commit: Add OoT text factory
df662c8 -> b9ab509  commit: Fix YAML generation and DList texture handling
b9ab509 -> 8f6da08  commit: Add __pycache__/ to gitignore
8f6da08 -> 5060b4c  commit: Fix OoT collision camera data for object collisions
5060b4c -> 1b50849  commit: Add OoT player animation header factory
1b50849 -> c3a478b  commit: Add 0-byte SkelLimbs marker files for skeleton exports
c3a478b -> ca84b21  commit: Add skeleton limb DList auto-discovery via AddAsset
ca84b21 -> f1e59b4  commit: Fix recursive external YAML deps and add --o2r-out option
f1e59b4 -> 6ccca79  commit: Skip VTX auto-creation for cross-file and autogen DLists
6ccca79 -> b8eff52  commit (amend): Skip VTX auto-creation for autogen DLists and alias/unconfigured segments
b8eff52 -> de8796b  commit: Fix SkinLimbDL VTX encoding for alias segments
de8796b -> e66ce38  commit: Fix code asset paths and VTX crash on virtual addresses
e66ce38 -> 06060ab  commit: Use ZAPD default segment 0x80 instead of hardcoded 6
06060ab -> e2005d2  commit: Handle ZAPD virtual segment 0x80 in address resolution
e2005d2 -> f05bcc5  commit: Treat Segment="0" as default 0x80 in zapd_to_torch
f05bcc5 -> 37ac74c  commit (amend): Treat Segment="0" as default 0x80 in zapd_to_torch
37ac74c -> 471202b  commit (amend): Treat Segment="0" as default 0x80 in zapd_to_torch
471202b -> 56a9a04  commit: Fix sCircleDList by handling virtual segment and junk opcodes in DList export
56a9a04 -> a4f5bd6  commit: Rename gTitleZeldaShieldLogoMQTex for non-MQ ROMs in zapd_to_torch
a4f5bd6 -> be3a29e  commit (amend): Rename gTitleZeldaShieldLogoMQTex for non-MQ ROMs in zapd_to_torch
be3a29e -> 383a041  commit: Handle LimbTable as 0-byte BLOB to match OTRExporter behavior
383a041 -> b4f7a8f  commit: Fix virtual segment VTX regression in overlay DLists
b4f7a8f -> 2f82e2d  commit: Add scene/room factory plan document
2f82e2d -> ea5972c  commit: Add OoT scene/room factory and fix scene YAML path mapping
ea5972c -> 3dbb02c  commit: Fix scene binary format mismatches for pathways, entrances, exits, and alt headers
3dbb02c -> 9f1d76a  commit: Include room YAMLs in test_assets.sh and add scene segment to room configs
9f1d76a -> 16c5cfa  commit: Fix room binary format: polyType size and meshData, update manifest
16c5cfa -> 0845e57  commit: Fix meshType 1 (background) room format and update manifest
0845e57 -> 958018e  commit: Add ZAPD-style VTX consolidation for scene/room DLists
958018e -> 1f6bab8  commit: Fix VTX consolidation scope and add MTX auto-discovery for scene DLists
1f6bab8 -> 87185e1  commit: Resolve VRAM matrix references (gMtxClear) in scene DLists
87185e1 -> a5ec48f  commit: Add G_BRANCH_Z target DList discovery and VTX consolidation
```

## Timeline

- **Branch created**: 2026-03-23 ~17:25 EDT (from upstream HEAD)
- **Last commit**: 2026-03-28 ~23:31 EDT (a5ec48f)
- **Author**: briaguya <70942617+briaguya0@users.noreply.github.com>
- **Total commits**: 65 (including 3 resets and 4 amends, ~58 unique changes)
- **Co-authors**: Claude Opus 4.6, Claude Sonnet 4.6

## Progress at time of loss

- **20,432 assets passing** (objects, overlays, code) with 0 failures
- **14,954 assets remaining**: 14,355 scenes (in progress), 598 audio, 1 portVersion
- Scene/room factory implemented and iterating on binary format correctness
- Last work was on VTX consolidation and G_BRANCH_Z DList discovery for scenes

## Recovery notes

- Source code recovered via ext4magic from raw filesystem blocks
- Multiple file versions recovered (different edit states); latest versions selected
- Git pack files for this repo were NOT recovered (packs from other repos were found)
- OoTTextFactory.cpp/.h were not recovered and will need to be recreated
- Some files that exist in upstream Torch were modified; diffs are visible in git
