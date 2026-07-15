# Sapphire vendor upstream snapshots

Byte-exact copies of pinned Sapphire sources used by `tools/vendor_sapphire.py`.

- Frontend reference: `SapphireRhodonite/melonDS-android@0.7.0.rc4`
- Core reference: `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

Refresh snapshots from local clones:

```bash
SAPPHIRE_ANDROID_ROOT=/path/to/melonDS-android \
SAPPHIRE_ANDROID_LIB_ROOT=/path/to/melonDS-android-lib \
python3 tools/vendor_sapphire.py --materialize-upstream --verify-upstream-snapshots
```

Verify committed snapshots:

```bash
python3 tools/vendor_sapphire.py --verify-upstream-snapshots
```
