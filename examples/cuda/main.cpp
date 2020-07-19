#include "MiniRun.hpp"
#include "cuda_interface.hpp"

void initialize(float* buffer, float val, int N)
{
    for (int i = 0; i < N; ++i) buffer[i] = val;
}

void check(bool& checkValid, float* buffer, float xv, float yv, float addVal, int N)
{
    const float expected = addVal * xv + yv;
    checkValid = false;
    printf("First %f expected: %f  addval %f xc %f yv %f \n", buffer[0], expected, addVal, xv, yv);
    for (int i = 0; i < N-1; ++i)
        if (std::abs(buffer[i] - expected) > 0.001)
        {
            printf("[%d] Fck %f %f %f \n", i, buffer[i], expected, std::abs(buffer[i] - expected));
            return;
        }

    checkValid = true;
}

int main()
{
    MiniRun run(5);

    int N = 1 << 10;

    int device = 0;
    float initXval = 2;
    float initYval = 2;
    float addVal = 2;
    setActive(device);
    void* stream = createStream();
    float* d_x = (float*)cMalloc(N * sizeof(float));
    float* d_y = (float*)cMalloc(N * sizeof(float));
    float* x = (float*)malloc(N * sizeof(float));
    float* y = (float*)malloc(N * sizeof(float));
    bool valid = true;



    run.createTask([&]() { initialize(x, initXval, N); }, {}, MiniRun::deps(x));
    run.createTask([&]() { initialize(y, initYval, N); }, {}, MiniRun::deps(y));
    run.createTask([&]() { setActive(device); copyToDevice(d_x, x, N*sizeof(float), stream); }, MiniRun::deps(x), MiniRun::deps(d_x));
    run.createTask([&]() { setActive(device); copyToDevice(d_y, y, N*sizeof(float), stream); }, MiniRun::deps(y), MiniRun::deps(d_y));
    run.createTask([&]() { setActive(device); saxpy(N, d_x, d_y, addVal, stream); }, MiniRun::deps(d_x), MiniRun::deps(d_y));

    run.createTask(
        [&]() {
            setActive(device);
            copyToHost(y, d_y, N * sizeof(float), stream);
        },
        [&]() {
            setActive(device);
            return streamEmpty(stream); //since we have enqueued all into a stream...
        }, MiniRun::deps(d_y), MiniRun::deps(y));

    run.createTask([&]() {check(valid, y, initXval, initYval, addVal, N); }, MiniRun::deps(y), MiniRun::deps(valid));
    run.taskwait();

    printf("The result is: %d\n", valid);

}