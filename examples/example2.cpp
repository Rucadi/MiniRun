#include "MiniRun.hpp"

using matrix_type = float;


void matmul(MiniRun& runtime, const size_t size, const matrix_type *a, const matrix_type *b,  matrix_type *c)
{
    runtime.createTask(
        [=]()
        {
            for(size_t k=0; k < size; ++k)
                for(size_t i=0; i < size; ++i)
                 for(size_t j=0; j < size; ++j)
                    c[i*size + j] += a[i*size + k] * b[k*size + j];
        }, MiniRun::deps(a,b), MiniRun::deps(c));
}

int main()
{

    MiniRun runtime;

    const size_t matrix_size = 1024*8;
    const size_t m2 = matrix_size*matrix_size;
    std::vector<matrix_type> a(m2),b(m2),c(m2);

    const size_t block_size = 128;
    const size_t b2 = block_size*block_size;
    const size_t bm = block_size*matrix_size;

    size_t numBlocks = matrix_size/block_size;

    printf("Creating tasks...\n");
    for(size_t i = 0; i < numBlocks; ++i)
    {
        for(size_t j = 0; j < numBlocks; ++j)
        {
            const size_t block_c_idx = j*b2 + i*bm;
            for(size_t k = 0; k < numBlocks; ++k)
            {
                const size_t block_a_idx = k*b2 + i*bm;
                const size_t block_b_idx = j*b2 + k*bm;
                matmul(runtime, block_size, &a[block_a_idx], &b[block_b_idx], &c[block_c_idx]);
            }
        }
    }
    printf("All tasks created, waiting the matrix multiply to end...\n");
    runtime.taskwait();
    printf("DONE\n");
    return 0;
}