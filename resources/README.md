# Starlight Resources

Put runtime assets and default config here.

During image build, files from this folder are copied recursively under root:

- `starlight/resources/cursor.png` -> `/starlight/cursor.png`
- `starlight/resources/config/settings.cfg` -> `/starlight/config/settings.cfg`

So Starlight can load both assets and config by filesystem path from `/starlight/...`.
