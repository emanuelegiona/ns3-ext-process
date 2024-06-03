# ExternalProcess: ns-3 module

This repository contains ExternalProcess, a simple module to facilitate running external processes within ns-3 simulations.

**Aim & Features**

- Custom program execution as a process parallel to ns-3 simulations

- Parallel process is started and kept alive until required

- Bi-directional communication with processes based on Unix named pipes

- Multiple parallel processes supported (each with own `ExternalProcess` instance)

- Non-interfering with Unix signals: watchdog thread for process supervision (Thanks [@vincenzosu][ghuser_vincenzo])

**Note:** This module is *currently NOT* intended for processes that have carry out operations asynchronously to the ns-3 simulation.

## Installation guidelines and documentation

Installation guidelines and detailed documentation for the latest version is hosted on [GitHub Pages][gh_pages_docs].

Documentation for legacy versions is stored as PDF files named `docs-v<tag>.pdf` in the [`docs` directory][legacy_docs] within this repository.

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

[ghuser_vincenzo]: https://github.com/vincenzosu
[gh_pages_docs]: https://emanuelegiona.github.io/ns3-ext-process/
[legacy_docs]: ./docs/
[cff]: https://citation-file-format.github.io/
[citation]: ./CITATION.cff
[senseslab]: https://senseslab.diag.uniroma1.it/
[license]: ./LICENSE
[ns3-license]: https://www.nsnam.org/develop/contributing-code/licensing/
