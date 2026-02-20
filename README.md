# Starlight

Starlight is the Desktop Environment for PeOS, maintained as a separable subproject.

## Build From Root Repository

```sh
make starlight
```

## Build Subproject Directly

```sh
make -C starlight ROOT_DIR=$(pwd)
```

The generated binary is written to `userland/bin/starlight` so it can be included in disk images.

## Config Files

Starlight loads persistent configuration from:

- `/system/config/display.cfg` (global OS config: `width`, `height`)
- `/starlight/config/settings.cfg` (DE config: `background`, `cursor`, `cursor_scale`)

Default DE config source:

- `starlight/resources/config/settings.cfg`

This file is copied automatically to `userland/data/starlight/config/settings.cfg`
during the build, together with the other assets under `starlight/resources/`.
If the config file is missing at runtime, Starlight regenerates it with default values.
