# Introduction

`ExternalProcess` is a simple ns-3 module to facilitate running external processes within ns-3 simulations.

**Aim & Features**

- Custom program execution as a process parallel to ns-3 simulations

- Parallel process is started and kept alive until required

- Bi-directional communication with processes based on Unix named pipes

- Multiple parallel processes supported (each with own `ExternalProcess` instance)

- Non-interfering with Unix signals: watchdog thread for process supervision (Thanks [@vincenzosu][ghuser_vincenzo])

**Note:** This module is *currently NOT* intended for processes that have carry out operations asynchronously to the ns-3 simulation.



[ghuser_vincenzo]: https://github.com/vincenzosu
