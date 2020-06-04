# MiniRun

MiniRun is a task-based runtime that follows OpenMP/OmpSs task model. The intention of this runtime is to demonstrate how a little runtime can be done, and to give task-support to projects that run in  multi-core systems but don't have access to OpenMP or OmpSs task-based runtimes, like VStudio compiler for Windows, wich doesn't support tasks.

There is no intention to support all the features of the two previous models, and the aim of this project is simplicity.  

## Dependences

When creating a task, we have to define which regions of the data the code is going to access. The model is the same as OpenMP or OmpSs with discrete dependences, the runtime only tracks the address of the symbol, not a range.

We have two types of dependence, IN and OUT dependences:

**IN** dependences are the ones that request the symbol for **READ ACCESS**
**OUT** dependences are the ones that request the symbol for **WRITE ACCESS**

While the runtime will allow multiple IN dependences in different tasks, only one OUT dependence can run over the same symbol at the same time.

## Usage

Creating a miniruntime:

When creating a miniruntime, you can decide the number of threads you want to use:


    #include  "MiniRun.hpp"
	
	MiniRun runtime();//Will use number_of_cpus-2 threads
	MiniRun runtime2(2);//Will use 2 threads
	 
    
The example shows the case of a block of a matmul which uses MiniRun:

      
	const int block = 128;
	void  block_matmul(const  float  *a, const  float  *b, float  *c)
	{
		runtime.createTask(
		[=]()
		{
			for (int k = 0; k < block; ++k)
				for (int i = 0; i < block; ++i)
					for (int j = 0; j < block; ++j)
					c[i*block + j] += a[i*block+ k] * b[k*block+ j];
		}, 	MiniRun::deps(a,b), MiniRun::deps(c));
	}

As seen in the example, the task creation is done as following:

    [runtime_object].createTask( [std::function<void()>], [IN_DEPS], [OUT_DEPS]); 

MiniRun::deps will accept any number of parameters, but you only can pass addresses to it.

## Compile

There is no hidden dependences when using MiniRun, include the header and compile.

 As this library uses std::threads, you must link against pthreads.

