# Packages

Global download cache of packages. Projects keep their own copy in `<project>/packages`; packages are fetched from the built in
registry (`config/registry.json` extends it) or from a git URL / archive, pinned by `lumen.lock.json`.
