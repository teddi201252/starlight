# Starlight

Starlight e' il Desktop Environment di PeOS, mantenuto come sottoprogetto separabile.

## Build dal root repo

```sh
make starlight
```

## Build diretto del sottoprogetto

```sh
make -C starlight ROOT_DIR=$(pwd)
```

Il binario prodotto finisce in `userland/bin/starlight` per essere incluso nelle immagini disco.
