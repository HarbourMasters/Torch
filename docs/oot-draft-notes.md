## what this does

generates an o2r that matches what https://github.com/briaguya0/Shipwright/tree/fix-skinvtxcnt-ub (just dev from when i started with https://github.com/HarbourMasters/ZAPDTR/pull/37 included) generates when using a PAL GC (sha1: 0227D7C0074F2D0AC935631990DA8EC5914597B4) rom. since zip files aren't generated deterministically the comparison was done file-by-file within the extracted archive.

## relevant files

* `zapd_to_torch.py`
  * does what it says on the tin, takes xml from ZAPDTR/OTRExporter land and adapts it to be torch yml
* `test_assets.sh`
  * used to verify, lots of options

## why this is POC/draft

* the `soh` dir shouldn't be in here
* i still need to actually review all the torch changes
  * some shared factory changes almost definitely break existing ports
  * there are some hacks/bugs ported from zapd for binary matching
* need to decide how much data we want to have in yml files
  * todo: fill in with info about the diff
* it only supports the 1 rom, i want to get it working with/verify against all supported roms

## things to look into

* external files in yml using pal_gc in the path, maybe this is because config.yml is in pal_gc's parent dir?
* what are the differences in how zapd handles this and how torch does
  * we read from o2r in zapd_to_torch, meaning we're getting extra info from there
  * DMA stuff too
