# =============================================================================
#  Makefile unico para CubeConquerPB
#
#  Compila TODO con un solo `make`:
#    1. CaDiCaL          -> cadical-api/.../build/libcadical.a
#    2. RoundingSat      -> roundingsat-dev-ipasir/build/librsipasir.so
#    3. Solver (src/)    -> src/pbsat  y  src/cubePB
#
#  Targets utiles:
#    make            (= make all)   compila dependencias + pbsat + cubePB
#    make pbsat                     solo el solver secuencial standalone
#    make cubePB                    solver cube&conquer (MPI) + dependencias
#    make cadical                   solo la libreria CaDiCaL
#    make roundingsat               solo la libreria RoundingSat
#    make clean                     borra objetos y binarios del solver
#    make distclean                 ademas limpia CaDiCaL y RoundingSat
# =============================================================================

CXX     = g++
MPICXX  = mpic++

# --- Directorios -------------------------------------------------------------
SRC          = src

CADICAL_DIR  = cadical-api/cadical-rel-3.0.0
CADICAL_LIB  = $(CADICAL_DIR)/build/libcadical.a
CADICAL_INC  = -I$(CADICAL_DIR)/src

RS_DIR       = roundingsat-dev-ipasir
RS_BUILD     = $(RS_DIR)/build
RS_LIB       = $(RS_BUILD)/librsipasir.so
RS_INC       = -I$(RS_DIR)/src
RS_LIBS      = -L$(RS_BUILD) -Wl,-rpath=$(abspath $(RS_BUILD)) -lrsipasir

# Fuentes de cada dependencia: si cambian, se rehace la libreria (asi se evita
# enlazar contra una .so/.a desactualizada).
CADICAL_SRCS = $(shell find $(CADICAL_DIR)/src -type f \( -name '*.cpp' -o -name '*.hpp' \))
RS_SRCS      = $(shell find $(RS_DIR)/src   -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \))

# --- Flags de compilacion ----------------------------------------------------
# Generacion automatica de dependencias de cabeceras (.d)
DEPFLAGS = -MMD -MP
CXXFLAGS = -std=c++20 -O3 -Wall -DNDEBUG $(RS_INC) $(CADICAL_INC) $(DEPFLAGS)

# --- Listas de objetos -------------------------------------------------------
COMMON_OBJS = $(addprefix $(SRC)/, \
	Branching.o \
	Cleanup.o \
	ConflictAnalysis.o \
	ConstraintValue.o \
	CubeConquerAPI.o \
	DBAddition.o \
	MaxHeap.o \
	Model.o \
	Parser.o \
	PBConstraint.o \
	Propagate.o \
	Solver.o \
	WConstraint.o)

PBSAT_OBJS  = $(SRC)/main.o $(COMMON_OBJS)

CUBEPB_OBJS = $(SRC)/cubePB.o \
	$(SRC)/NativeBackend.o \
	$(SRC)/RoundingSatBackend.o \
	$(SRC)/CadicalBackend.o \
	$(COMMON_OBJS)

ALL_OBJS = $(sort $(PBSAT_OBJS) $(CUBEPB_OBJS))

PBSAT_BIN  = $(SRC)/pbsat
CUBEPB_BIN = $(SRC)/cubePB

# =============================================================================
#  Targets principales
# =============================================================================
.PHONY: all pbsat cubePB clean distclean cadical roundingsat

all: $(PBSAT_BIN) $(CUBEPB_BIN)

# Alias comodos (los targets "reales" son los ficheros, para que make rastree
# correctamente su estado y un build incremental no reenlace sin necesidad).
pbsat:  $(PBSAT_BIN)
cubePB: $(CUBEPB_BIN)

$(PBSAT_BIN): $(PBSAT_OBJS)
	$(CXX) $(CXXFLAGS) $(PBSAT_OBJS) -o $@

$(CUBEPB_BIN): $(CUBEPB_OBJS) $(CADICAL_LIB) $(RS_LIB)
	$(MPICXX) $(CXXFLAGS) $(CUBEPB_OBJS) $(RS_LIBS) $(CADICAL_LIB) -o $@

# =============================================================================
#  Reglas de compilacion de objetos (en src/)
# =============================================================================
# Regla generica: g++.  Los objetos que usan MPI tienen su regla explicita
# debajo y prevalecen sobre esta.
$(SRC)/%.o: $(SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Objetos que requieren MPI -> mpic++
$(SRC)/cubePB.o: $(SRC)/cubePB.cpp
	$(MPICXX) $(CXXFLAGS) -c $< -o $@

$(SRC)/CadicalBackend.o: $(SRC)/CadicalBackend.cpp
	$(MPICXX) $(CXXFLAGS) -c $< -o $@

# =============================================================================
#  Dependencias externas
# =============================================================================
# --- CaDiCaL ---
# Si no existe el makefile generado por ./configure, lo generamos primero.
cadical: $(CADICAL_LIB)

$(CADICAL_LIB): $(CADICAL_SRCS)
	@if [ ! -f $(CADICAL_DIR)/makefile ]; then \
		echo ">>> Configurando CaDiCaL"; \
		cd $(CADICAL_DIR) && ./configure; \
	fi
	@echo ">>> Compilando CaDiCaL"
	$(MAKE) -C $(CADICAL_DIR)

# --- RoundingSat (cmake) ---
# Si no existe la cache de cmake, configuramos el build dir primero.
roundingsat: $(RS_LIB)

$(RS_LIB): $(RS_SRCS)
	@if [ ! -f $(RS_BUILD)/CMakeCache.txt ]; then \
		echo ">>> Configurando RoundingSat (cmake)"; \
		cmake -S $(RS_DIR) -B $(RS_BUILD) -DCMAKE_BUILD_TYPE=Release; \
	fi
	@echo ">>> Compilando RoundingSat"
	$(MAKE) -C $(RS_BUILD)

# =============================================================================
#  Limpieza
# =============================================================================
clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) \
	      $(SRC)/pbsat $(SRC)/cubePB \
	      $(SRC)/intsatSolution.lp $(SRC)/intsatSolution.pl

distclean: clean
	-$(MAKE) -C $(CADICAL_DIR) clean
	-$(MAKE) -C $(RS_BUILD) clean

# --- Inclusion de dependencias de cabeceras autogeneradas --------------------
-include $(ALL_OBJS:.o=.d)
