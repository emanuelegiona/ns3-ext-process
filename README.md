# ExternalProcess: ns-3 module

<sub>Tested via [ns3-compatibility-action][gha-test] on [egiona/ns3-base][docker-imgs] Docker images</sub>

[![.github/workflows/ns-3.35.yml](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.35.yml/badge.svg?branch=tests)](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.35.yml) 
[![.github/workflows/ns-3.40.yml](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.40.yml/badge.svg?branch=tests)](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.40.yml) 
[![.github/workflows/ns-3.41.yml](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.41.yml/badge.svg?branch=tests)](https://github.com/emanuelegiona/ns3-ext-process/actions/workflows/ns-3.41.yml)

This repository contains ExternalProcess, a simple module to facilitate running external processes within ns-3 simulations.

**Aim & Features**

- Custom program execution as a process parallel to ns-3 simulations

- Parallel process is started and kept alive until required

- Bi-directional communication with processes based on TCP sockets

- Multiple parallel processes supported (each with own `ExternalProcess` instance)

- Non-interfering with Unix signals: watchdog thread for process supervision (Thanks [@vincenzosu][ghuser_vincenzo])

**Note:** This module is *currently NOT* intended for processes that have to carry out operations asynchronously to the ns-3 simulation. This means that all communications to an external process are blocking.

## Installation guidelines and documentation

Installation guidelines and detailed documentation for the latest version is hosted on [GitHub Pages][gh_pages_docs].

Documentation for legacy versions is stored as PDF files named `docs-<tag>.pdf` in the [`docs` directory][legacy_docs] within this repository.

Full changelog can be found at [this page][changes].

## Citing this work

If you use the module in this repository, please cite this work using any of the following methods:

**APA**

```
Giona, E. ns3-ext-process [Computer software]. https://doi.org/10.5281/zenodo.8172121
```

**BibTeX**

```
@software{Giona_ns3-ext-process,
author = {Giona, Emanuele},
doi = {10.5281/zenodo.8172121},
license = {GPL-2.0},
title = {{ns3-ext-process}},
url = {https://github.com/emanuelegiona/ns3-ext-process}
}
```

Bibliography entries generated using [Citation File Format][cff] described in the [CITATION.cff][citation] file.

## License

**Copyright (c) 2023 Emanuele Giona ([SENSES Lab][senseslab], Sapienza University of Rome)**

This repository is distributed under [GPLv2 license][license].

ns-3 is distributed via its own [license][ns3-license] and shall not be considered part of this work.



[gha-test]: https://github.com/emanuelegiona/ns3-compatibility-action
[docker-imgs]: https://github.com/emanuelegiona/ns3-base-docker
[ghuser_vincenzo]: https://github.com/vincenzosu
[gh_pages_docs]: https://emanuelegiona.github.io/ns3-ext-process/
[legacy_docs]: ./docs/
[changes]: ./CHANGELOG.md
[cff]: https://citation-file-format.github.io/
[citation]: ./CITATION.cff
[senseslab]: https://senseslab.diag.uniroma1.it/
[license]: ./LICENSE
[ns3-license]: https://www.nsnam.org/develop/contributing-code/licensing/
