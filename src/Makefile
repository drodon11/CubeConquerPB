CC              = g++
MPICC           = mpic++
#CFLAGS          = -std=c++20 -pg -O3 -Wall
#CFLAGS          = -std=c++20 -O3 -g -D_GLIBCXX_DEBUG
#CFLAGS          = -std=c++20 -g -O3 -Wall -D_GLIBCXX_DEBUG #-DNDEBUG 
#CFLAGS          = -std=c++20 -g -pg -O3 -Wall -DNDEBUG 
#CFLAGS          = -std=c++20 -g -O0 -fno-inline -Wall -D_GLIBCXX_DEBUG
#the following is for valgrind --leak-check=yes intsat c.lp
#CFLAGS          = -g -O0 -Wall

# for measuring time
CFLAGS          = -std=c++20 -O3 -Wall  -DNDEBUG

# for checking correctness
#CFLAGS          = -std=c++20 -O3 


# ifndef d
# CFLAGS += -DNDEBUG
# endif

SRCS =  main.cpp \
	Branching.cpp \
	Cleanup.cpp \
	ConflictAnalysis.cpp \
	ConstraintValue.cpp \
	CubeConquerAPI.cpp \
	DBAddition.cpp \
	MaxHeap.cpp \
	Model.cpp \
	Parser.cpp \
	PBConstraint.cpp \
	Propagate.cpp \
	Solver.cpp \
	WConstraint.cpp 

OBJS = $(addsuffix .o, $(basename $(SRCS) .cpp))

CUBEPB_OBJS = cubePB.o \
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
	WConstraint.o

all: pbsat cubePB

pbsat: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o pbsat

cubePB: $(CUBEPB_OBJS)
	$(MPICC) $(CFLAGS) $(CUBEPB_OBJS) -o cubePB

%.o: %.cpp
	$(CC) $(CFLAGS) -c $*.cpp

cubePB.o: cubePB.cpp
	$(MPICC) $(CFLAGS) -c cubePB.cpp

clean:
	rm -f *.o intsatSolution.lp intsatSolution.pl pbsat cubePB

depend:
	makedepend -Y $(SRCS) cubePB.cpp


# DO NOT DELETE

main.o: Parser.h WConstraint.h Constraint.h Functions.h PBConstraint.h
main.o: Clause.h Model.h Reason.h MaxHeap.h Strategy.h Statistics.h Solver.h
main.o: OccurListVector.h
cubePB.o: Parser.h Solver.h WConstraint.h
Branching.o: Solver.h PBConstraint.h Constraint.h Functions.h WConstraint.h
Branching.o: Clause.h Model.h Reason.h Strategy.h Statistics.h MaxHeap.h
Branching.o: OccurListVector.h
Cleanup.o: Solver.h PBConstraint.h Constraint.h Functions.h WConstraint.h
Cleanup.o: Clause.h Model.h Reason.h Strategy.h Statistics.h MaxHeap.h
Cleanup.o: OccurListVector.h
ConflictAnalysis.o: Solver.h PBConstraint.h Constraint.h Functions.h
ConflictAnalysis.o: WConstraint.h Clause.h Model.h Reason.h Strategy.h
ConflictAnalysis.o: Statistics.h MaxHeap.h OccurListVector.h
ConstraintValue.o: Solver.h PBConstraint.h Constraint.h Functions.h
ConstraintValue.o: WConstraint.h Clause.h Model.h Reason.h Strategy.h
ConstraintValue.o: Statistics.h MaxHeap.h OccurListVector.h
CubeConquerAPI.o: Solver.h PBConstraint.h Constraint.h Functions.h
CubeConquerAPI.o: WConstraint.h Clause.h Model.h Reason.h Strategy.h
CubeConquerAPI.o: Statistics.h MaxHeap.h OccurListVector.h
DBAddition.o: Solver.h PBConstraint.h Constraint.h Functions.h WConstraint.h
DBAddition.o: Clause.h Model.h Reason.h Strategy.h Statistics.h MaxHeap.h
DBAddition.o: OccurListVector.h
MaxHeap.o: MaxHeap.h
Model.o: Model.h Reason.h Clause.h WConstraint.h Constraint.h Functions.h
Model.o: PBConstraint.h
Parser.o: Parser.h WConstraint.h Constraint.h Functions.h PBConstraint.h
Parser.o: Clause.h Model.h Reason.h
PBConstraint.o: Functions.h PBConstraint.h Constraint.h WConstraint.h
PBConstraint.o: Clause.h Model.h Reason.h
Propagate.o: Solver.h PBConstraint.h Constraint.h Functions.h WConstraint.h
Propagate.o: Clause.h Model.h Reason.h Strategy.h Statistics.h MaxHeap.h
Propagate.o: OccurListVector.h
Solver.o: Solver.h PBConstraint.h Constraint.h Functions.h WConstraint.h
Solver.o: Clause.h Model.h Reason.h Strategy.h Statistics.h MaxHeap.h
Solver.o: OccurListVector.h
WConstraint.o: WConstraint.h Constraint.h Functions.h PBConstraint.h Clause.h
WConstraint.o: Model.h Reason.h
