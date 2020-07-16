
# MiniRun

MiniRun is a task-based runtime that follows OpenMP/OmpSs task model. The intention of this runtime is to demonstrate how a little runtime can be done, and to give task-support to projects that run in  multi-core systems but don't have access to OpenMP or OmpSs task-based runtimes, like VStudio compiler for Windows, wich doesn't support tasks.

There is no intention to support all the features of the two previous models, and the aim of this project is simplicity.  

## Dependences

When creating a task, we have to define which regions of the data the code is going to access. The model is the same as OpenMP or OmpSs with discrete dependences, the runtime only tracks the address of the symbol, not a range.

We have two types of dependence, IN and OUT dependences:

**IN** dependences are the ones that request the symbol for **READ ACCESS**
**OUT** dependences are the ones that request the symbol for **WRITE ACCESS**

While the runtime will allow multiple IN dependences in different tasks, only one OUT dependence can run over the same symbol at the same time.

# Usage

Creating a miniruntime:

When creating a miniruntime, you can decide the number of threads you want to use:


    #include  "MiniRun.hpp"
	
	MiniRun runtime();//Will use number_of_cpus-1 threads
	MiniRun runtime2(2);//Will use 2 threads
The life of the runtime is strictly tied to the life of the MiniRun objects, multiple runtimes with an arbritrary number of threads can coexists, the penalty for creating/destroying a runtime is only the thread creation/destruction.
	 


## DEPENDENCY SPECIFICATION
The IN_DEPS and OUT_DEPS are the **INPUT** and **OUTPUT** dependences. In order to specify this dependences, a templated vector creation is done, to create a dependency list, we need to use MiniRun::deps.

MiniRun::deps accepts any number of parameters, and if a pointer is passed, will use the pointer for tracking, and if an object is passed, it will try to get the pointer to that object. The construct is as follows:
 
[IN_DEPS] || [OUT_DEPS]  =      MiniRun::deps( <obj1>...); 

## GROUPS

When creating a task, we can specify a **GROUP**,  each group in the runtime is indepdendent on each other, and we can decide to synchronize for an specific group or for all tasks indepdendently of the group.

If no group is specified, 0 is used as group.

## TASK CREATION
For creating a task, we will make use of the function "createTask". 

As a parameter, it accepts a lambda or  std::function. The data initialization follows the LAMBDA capture list specification, since the runtime will run the function object as created. You can get more information here: [https://www.learncpp.com/cpp-tutorial/lambda-captures/](https://www.learncpp.com/cpp-tutorial/lambda-captures/)

    [runtime_object].createTask( [std::function<void()>], [IN_DEPS], [OUT_DEPS], [GROUP]); 

## TASKWAIT

A taskwait is the synchronization point, which will block the execution of the thread that runs it until the tasks have finished executing. 

In MiniRun, we have two taskwait constructs, 
 
    [runtime_object].createTask(); 
    [runtime_object].createTask([GROUP]); 

The first one, will wait until the execution of all the tasks, and the second one, will wait until the execution of all the group tasks.

While "blocked" at the taskwait, the taskwait thread will be used for executing tasks.

# EMSCRIPTEN

Since emscripten supports threading, and this runtime has no dependences, it can be used in web applications using the emscripten compiler, without any modifications to the code.

Example of compilation and running with emscripten:
	 em++ main.cpp -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=8 -s TOTAL_MEMORY=2047mb
	 node --experimental-wasm-threads --experimental-wasm-bulk-memory  ./a.out.js 
# EXAMPLES
  ## Basic example: Matmul block
The example shows the case of a block of a matrix multiply which uses MiniRun:

      
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
	

# Basic example: Fibonnaci numbers
(Never ever program it this way, this only serves as demonstration, a n too big will cause to stack-overflow)

    MiniRun  run;
    std::atomic<int> group(0);
    int  fib(int  n)  
    {
        if (n<2) return  n;   
        int  i,j;
	    int  a_group = group++;
	    run.createTask([&, n = n-1](){ i=fib(n); },a_group);
	    run.createTask([&, n = n-2](){ j=fib(n); },a_group);
	    run.taskwait(a_group);
	    return  i+j;
    }
    
    int  main(){ int n=25; printf ("fib(%d) = %d\n", n, fib(n)); }


## Compile

There is no hidden dependences when using MiniRun, include the header and compile.

 As this library uses std::threads, you must link against pthreads.


