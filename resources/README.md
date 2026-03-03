# Starlight Resources

Put runtime assets and default config here.

During image build, files from this folder are copied recursively under root:

- `starlight/resources/cursor.png` -> `/starlight/cursor.png`
- `starlight/resources/config/settings.cfg` -> `/starlight/config/settings.cfg`
- `starlight/resources/config/theme.cfg` -> `/starlight/config/theme.cfg`
- `starlight/resources/fonts/default.font` -> `/starlight/fonts/default.font`

So Starlight can load both assets and config by filesystem path from `/starlight/...`.
