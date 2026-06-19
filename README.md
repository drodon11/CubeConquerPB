# CubeConquerPB

Solver de Programación Pseudo-Booleana (PB) basado en la técnica **Cube & Conquer**:
un solver PB nativo, secuencial, particiona el problema en "cubos" mediante
ramificación y los distribuye entre varios procesos MPI, que los resuelven en
paralelo apoyándose en distintos backends SAT/PB intercambiables (nativo,
[CaDiCaL](https://github.com/arminbiere/cadical) o
[RoundingSat](https://gitlab.com/MIAOresearch/software/roundingsat)).

Trabajo de Fin de Grado — Facultat d'Informàtica de Barcelona (UPC).

## Estructura del repositorio

```
src/                    Código fuente del solver (parser, motor PB secuencial,
                         backends y el driver Cube & Conquer cubePB.cpp)
cadical-api/            CaDiCaL 3.0.0 (vendorizado) usado como backend SAT
roundingsat-dev-ipasir/ RoundingSat con interfaz IPASIR (vendorizado), backend PB
benchs/                 Instancias de ejemplo (.lp) para pruebas rápidas
OPT/                    Logs crudos de los experimentos lanzados en el clúster
benchmarks.xlsx         Resultados de los experimentos ya procesados
Makefile                Build único: compila CaDiCaL, RoundingSat y el solver
```

`OPT/` y `benchmarks.xlsx` están excluidos de los `tar`/`zip` generados con
`git archive` (ver `.gitattributes`) por su tamaño; siguen versionados en el
repositorio.

## Requisitos

- `g++` con soporte C++20
- Una implementación de MPI (`mpic++`, p. ej. OpenMPI)
- `cmake` >= 3.15
- Boost >= 1.70 y GMP (dependencias de RoundingSat)

## Compilación

Un único `make` en la raíz compila las dos dependencias vendorizadas
(CaDiCaL y RoundingSat) y los dos binarios del proyecto:

```sh
make             # = make all: dependencias + pbsat + cubePB
make pbsat       # solo el solver secuencial standalone
make cubePB      # solver Cube & Conquer (MPI) + dependencias
make cadical     # solo la librería CaDiCaL
make roundingsat # solo la librería RoundingSat
make clean       # borra objetos y binarios del solver
make distclean   # además limpia los builds de CaDiCaL y RoundingSat
```

Los binarios resultantes son `src/pbsat` y `src/cubePB`.

## Uso

### `pbsat` — solver secuencial

```sh
src/pbsat [-help] [-seed int] [-tlimit int] [-bt0 bool] [-d decisionsLimit]
          [-c conflictsLimit] [-strategy file.txt] [-decision file.txt]
          [-iniSol file.txt] formula.opb|formula.lp
```

### `cubePB` — Cube & Conquer (MPI)

```sh
mpirun -np N ./src/cubePB formula.opb|formula.lp|formula.cnf \
       [--solver=native|--solver=roundingsat|--solver=cadical]
```

Se requieren al menos 2 procesos (`N >= 2`): el proceso 0 actúa de máster
(genera y reparte los cubos) y el resto de workers resuelven cada cubo con
el backend indicado. `--solver=cadical` solo admite instancias `.cnf`.

Formatos de entrada soportados: `.opb` (pseudo-Boolean), `.lp` y `.cnf`.

## Licencias de terceros

`cadical-api/cadical-rel-3.0.0` (MIT) y `roundingsat-dev-ipasir` (ver su
`LICENSE`) son código de terceros vendorizado para poder compilar el
proyecto de forma autocontenida; conservan su licencia original.
