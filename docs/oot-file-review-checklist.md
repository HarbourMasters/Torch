# File-by-file review checklist

Legend: [x] reviewed, [~] needs follow-up before PR, [ ] not yet reviewed

## Modified files (existing Torch code)

- [x] CMakeLists.txt
- [~] .gitignore — `!soh/tools/` and trailing comment will need cleanup when soh/ is removed for PR
- [x] src/Companion.cpp
- [x] src/Companion.h
- [x] src/factories/BaseFactory.h
- [x] src/factories/BlobFactory.cpp
- [ ] src/factories/DisplayListFactory.cpp
- [ ] src/factories/DisplayListFactory.h
- [x] src/factories/ResourceType.h
- [x] src/n64/Cartridge.cpp
- [x] src/utils/Decompressor.cpp

## New files — Torch core

- [x] src/AliasManager.cpp
- [x] src/AliasManager.h
- [x] lib/libyaz0/yaz0.c
- [x] lib/libyaz0/yaz0.h

## New files — OoT factories

- [x] src/factories/oot/DeferredVtx.cpp
- [x] src/factories/oot/DeferredVtx.h
- [x] src/factories/oot/OoTAnimationFactory.cpp
- [x] src/factories/oot/OoTAnimationFactory.h
- [x] src/factories/oot/OoTAnimationTypes.h
- [x] src/factories/oot/OoTArrayFactory.cpp
- [x] src/factories/oot/OoTArrayFactory.h
- [x] src/factories/oot/OoTAudioFactory.cpp
- [x] src/factories/oot/OoTAudioFactory.h
- [x] src/factories/oot/OoTAudioTypes.h
- [x] src/factories/oot/OoTAudioTypes.cpp
- [x] src/factories/oot/OoTAudioSequenceWriter.h
- [x] src/factories/oot/OoTAudioSequenceWriter.cpp
- [x] src/factories/oot/OoTAudioSampleWriter.h
- [x] src/factories/oot/OoTAudioSampleWriter.cpp
- [x] src/factories/oot/OoTAudioFontWriter.h
- [x] src/factories/oot/OoTAudioFontWriter.cpp
- [x] src/factories/oot/OoTCollisionFactory.cpp
- [x] src/factories/oot/OoTCollisionFactory.h
- [x] src/factories/oot/OoTCurveAnimationFactory.cpp
- [x] src/factories/oot/OoTCurveAnimationFactory.h
- [x] src/factories/oot/OoTCutsceneFactory.cpp
- [x] src/factories/oot/OoTCutsceneFactory.h
- [x] src/factories/oot/OoTLimbFactory.cpp
- [x] src/factories/oot/OoTLimbFactory.h
- [x] src/factories/oot/OoTMtxFactory.cpp
- [x] src/factories/oot/OoTMtxFactory.h
- [x] src/factories/oot/OoTPathFactory.cpp
- [x] src/factories/oot/OoTPathFactory.h
- [x] src/factories/oot/OoTPlayerAnimationFactory.cpp
- [x] src/factories/oot/OoTPlayerAnimationFactory.h
- [x] src/factories/oot/OoTSceneCommandWriter.cpp
- [x] src/factories/oot/OoTSceneCommandWriter.h
- [x] src/factories/oot/OoTSceneFactory.cpp
- [x] src/factories/oot/OoTSceneFactory.h
- [x] src/factories/oot/OoTSceneUtils.cpp
- [x] src/factories/oot/OoTSceneUtils.h
- [x] src/factories/oot/OoTSkeletonFactory.cpp
- [x] src/factories/oot/OoTSkeletonFactory.h
- [x] src/factories/oot/OoTSkeletonTypes.cpp
- [x] src/factories/oot/OoTSkeletonTypes.h
- [x] src/factories/oot/OoTTextFactory.cpp
- [x] src/factories/oot/OoTTextFactory.h

## New files — SoH tooling / data (not Torch code)

- [~] Entire soh/ directory will be removed before PR — review deferred

## Cross-cutting concerns (check across all changed/new files)

- [ ] Debug logging cleanup — SPDLOG_WARN used for debug tracing, should be removed or downgraded
- [ ] Static functions → private methods with declarations in headers
- [ ] Prefer T& over shared_ptr for methods that don't need ownership
- [ ] Missing #ifdef OOT_SUPPORT guards

## PR notes

- Cross-font stack residue in OoTAudioFontWriter — replicates ZAPDTR's uninitialized stack behavior for invalid instruments. See docs/oot-audio-font-residue-analysis.md. Worth calling out for reviewers.

## Docs

- [x] docs/bswap32-investigation.md
- [ ] docs/ (remaining — evaluate which to keep vs remove before PR)
