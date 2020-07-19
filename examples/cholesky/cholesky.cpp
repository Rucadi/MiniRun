#include "MiniRun.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <thread>
#include "cholesky.hpp"

#define VERBOSE
constexpr int NUM_THREADS = 8;
MiniRun runtime(NUM_THREADS-1);

void omp_potrf(MiniRun& runtime, double * const A, int ts, int ld)
{
   static int INFO;
   static char L = 'L';
   
   const auto OUT = MiniRun::deps(A);
   runtime.createTask([=](){dpotrf_(&L, &ts, A, &ld, &INFO);},{},OUT);
}

void omp_trsm(MiniRun& runtime, double *A, double *B, int ts, int ld)
{
   static char LO = 'L', TR = 'T', NU = 'N', RI = 'R';
   static double DONE = 1.0;
   
   const auto IN  = MiniRun::deps(A);
   const auto OUT = MiniRun::deps(B);
   runtime.createTask([=](){dtrsm_(&RI, &LO, &TR, &NU, &ts, &ts, &DONE, A, &ld, B, &ld );}, IN, OUT);
   
}

void omp_syrk(MiniRun& runtime, double *A, double *B, int ts, int ld)
{
   static char LO = 'L', NT = 'N';
   static double DONE = 1.0, DMONE = -1.0;
   const auto IN  = MiniRun::deps(A);
   const auto OUT = MiniRun::deps(B);
   runtime.createTask([=](){dsyrk_(&LO, &NT, &ts, &ts, &DMONE, A, &ld, &DONE, B, &ld );}, IN, OUT);

}

void omp_gemm(MiniRun& runtime, double *A, double *B, double *C, int ts, int ld)
{
   static const char TR = 'T', NT = 'N';
   static double DONE = 1.0, DMONE = -1.0;
   const auto IN  = MiniRun::deps(A,B);
   const auto OUT = MiniRun::deps(C);
   runtime.createTask([=](){dgemm_(&NT, &TR, &ts, &ts, &ts, &DMONE, A, &ld, B, &ld, &DONE, C, &ld);}, IN, OUT);
}

void cholesky_blocked(int numThreads, const int ts, const int nt, double** Ah)
{
   MiniRun runtime(numThreads);
   const auto ah = [&](auto i, auto j)->double*{ return Ah[nt*i+j]; };	
   for (int k = 0; k < nt; k++) {

      // Diagonal Block factorization
      omp_potrf (runtime, ah(k,k), ts, ts);

      // Triangular systems
      for (int i = k + 1; i < nt; i++) {
         omp_trsm (runtime, ah(k,k), ah(k,i), ts, ts);
      }

      // Update trailing matrix
      for (int i = k + 1; i < nt; i++) {
         for (int j = k + 1; j < i; j++) {
            omp_gemm (runtime, ah(k,i), ah(k,j), ah(j,i), ts, ts);
         }
         omp_syrk (runtime, ah(k,i), ah(i,i), ts, ts);
      }

   }
   //implicit taskwait
}

int main(int argc, char* argv[])
{
   char *result[3] = {(char*)"n/a",(char*)"sucessful",(char*)"UNSUCCESSFUL"};
   const double eps = BLAS_dfpinfo( blas_eps );

   if ( argc < 4) {
      printf( "cholesky matrix_size block_size check numThreads\n" );
      exit( -1 );
   }
   const int  n = atoi(argv[1]); // matrix size
   const int ts = atoi(argv[2]); // tile size
   int check    = atoi(argv[3]); // check result?

   int numThreads   = std::thread::hardware_concurrency();
   if(argc>=5) numThreads =   atoi(argv[4]); // numthreads

   // Allocate matrix
   double * const matrix = (double *) malloc(n * n * sizeof(double));
   assert(matrix != NULL);

   // Init matrix
   initialize_matrix(n, ts, matrix);

   // Allocate matrix
   double * const original_matrix = (double *) malloc(n * n * sizeof(double));
   assert(original_matrix != NULL);

   const int nt = n / ts;

   // Allocate blocked matrix
   double *Ah[nt][nt];

   for (int i = 0; i < nt; i++) {
      for (int j = 0; j < nt; j++) {
         Ah[i][j] = (double*) malloc(ts * ts * sizeof(double));
         assert(Ah[i][j] != NULL);
      }
   }

   for (int i = 0; i < n * n; i++ ) {
      original_matrix[i] = matrix[i];
   }

#ifdef VERBOSE
   printf ("Executing ...\n");
#endif

   convert_to_blocks(ts, nt, n, (double*) matrix, (double**) Ah);

   const float t1 = get_time();
   cholesky_blocked( numThreads,ts, nt, (double**) Ah);

   const float t2 = get_time() - t1;
   convert_to_linear(ts, nt, n, (double**) Ah, (double*) matrix);

   if ( check ) {
      const char uplo = 'L';
      if ( check_factorization( n, original_matrix, matrix, n, uplo, eps) ) check++;
   }

   free(original_matrix);

   float time = t2;
   float gflops = (((1.0 / 3.0) * n * n * n) / ((time) * 1.0e+9));

   // Print results
#ifdef VERBOSE
   printf( "============ CHOLESKY RESULTS ============\n" );
   printf( "  matrix size:          %dx%d\n", n, n);
   printf( "  block size:           %dx%d\n", ts, ts);
   printf( "  number of threads:    %d\n", NUM_THREADS);
   printf( "  time (s):             %f\n", time);
   printf( "  performance (gflops): %f\n", gflops);
   printf( "  result :              %s\n",  result[check]);
   printf( "==========================================\n" );
#else
   printf( "test:%s-%d-%d:threads:%2d:result:%s:gflops:%f\n", argv[0], n, ts, NUM_THREADS, result[check], gflops );
#endif

   // Free blocked matrix
   for (int i = 0; i < nt; i++) {
      for (int j = 0; j < nt; j++) {
         assert(Ah[i][j] != NULL);
         free(Ah[i][j]);
      }
   }

   // Free matrix
   free(matrix);

   return 0;
}

