nvcc -c cuda_interface.cu  -o cuda.o
clang++ cuda.o main.cpp   -lpthread -lcuda -lcudart