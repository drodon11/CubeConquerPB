# RoundingSat

## The pseudo-Boolean solver powered by proof complexity!

RoundingSat solves decision and optimization problems formulated as 0-1 integer linear programs.

## Features

- Native conflict analysis over 0-1 integer linear constraints, constructing full-blown cutting planes proofs.
- Highly efficient watched propagation routines.
- Seamless use of multiple precision arithmetic.
- Optional integration with the SoPlex LP solver.
- Linear and core-guided optimization.
- Certified unsatisfiability and optimality.

All of these combine to make RoundingSat the world's fastest pseudo-Boolean solver.

## Pre-built binaries

- [Linux x86_64 static](../-/jobs/artifacts/master/file/build/roundingsat?job=build-linux)
- [Windows x64 static](../-/jobs/artifacts/master/file/build/roundingsat.exe?job=build-windows)

## Usage

RoundingSat takes as input a linear Boolean formula / 0-1 integer linear program, and outputs a(n optimal) solution or reports that none exists.

    roundingsat test/instances/opb/opt/stein15.opb

RoundingSat natively supports three input formats:
- pseudo-Boolean PBO format (only linear objective and constraints)
- DIMACS CNF (conjunctive normal form)
- Weighted CNF

For a description of these input formats, see [here](InputFormats.md).

Other common formats such as MPS or LP can be easily translated into OPB, see `tools/mpstoopb.sh`.

The default set of parameters works well on most problems, but different settings work better on particular problems. If RoundingSat takes too long to solve your problem, please contact us.

## Compilation

In the root directory of RoundingSat:

    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

For a debug build:

    cd build_debug
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make

### Compilation on Windows

When building with Visual Studio on Windows, it is recommended to use the vcpkg package manager together with the NMake build system. First, install the required dependencies as detailed below. To use NMake, launch the "Developer Power Shell for VS" and run

    cd build
    cmake -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ..
    nmake
    
## Dependencies

- C++20 (i.e., a reasonably recent compiler)
- Boost library: https://www.boost.org
- Optional: SoPlex LP solver (see below)

### Installing dependencies on Debian and derivatives (Ubuntu,...)

    apt install build-essential cmake libboost-all-dev

### Installing dependencies on Windows using vcpkg
`vcpkg` is a cross-platform C/C++ package manager maintained by Microsoft. Launch the "Developer Power Shell for VS" and run

    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg && .\bootstrap-vcpkg.bat
    .\vcpkg.exe install boost-multiprecision:x64-windows
    $env:VCPKG_ROOT = "C:\path\to\vcpkg"

## SoPlex

RoundingSat uses the LP solver SoPlex to improve its search routine. To disable SoPlex at compile time, configure RoundingSat with the cmake option `-Dsoplex=OFF`:

    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -Dsoplex=OFF ..
    make

By default the build system will download the appropriate version of SoPlex. Alternatively, the location of the SoPlex package can be configured with the cmake option `-Dsoplex_pkg=<location>`. SoPlex can also be disabled at run time by passing option `--lp=0` to RoundingSat.

## Citations

Origin paper with a focus on cutting planes conflict analysis:
**[EN18]** J. Elffers, J. Nordström. Divide and Conquer: Towards Faster Pseudo-Boolean Solving. *IJCAI 2018*

Integration with SoPlex:
**[DGN20]** J. Devriendt, A. Gleixner, J. Nordström. Learn to Relax: Integrating 0-1 Integer Linear Programming with Pseudo-Boolean Conflict-Driven Search. *CPAIOR 2020 / Constraints journal*

Watched propagation:
**[D20]** J. Devriendt. Watched Propagation for 0-1 Integer Linear Constraints. *CP 2020*

Core-guided optimization:
**[DGDNS21]** J. Devriendt, S. Gocht, E. Demirović, J. Nordström, P. J. Stuckey. Cutting to the Core of Pseudo-Boolean Optimization: Combining Core-Guided Search with Cutting Planes Reasoning. *AAAI 2021*

## Run inside a Docker container

If the compilation procedure above does not work, an alternative is to build a Docker image and run RoundingSAT as a Docker container.

A pre-built Docker image can be found on [Docker Hub](https://hub.docker.com/r/aoer/roundingsat).

We also provide a `Dockerfile` for creating the RoundingSAT Docker image.

### Running the Docker image:

If you pulled the [image from Docker Hub](https://docs.docker.com/engine/install/) run:
```bash
docker run -v /path/to/instance:/instance aoer/roundingsat [options] /instance/filename.opb
```

If you compiled the the image yourself:
```bash
docker run -v /path/to/instance:/instance roundingsat [options] /instance/filename.opb
```

The `-v` option mounts the host machines directory `/path/to/instance` at the directory `/instance`.

### Compiling the Docker image:

1. Make sure that you have [Docker installed](https://docs.docker.com/engine/install/).
2. Run the following command to build the Docker image:
```bash
docker build -t roundingsat .
```
