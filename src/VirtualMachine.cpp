#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
#include <queue>

using namespace std;

extern "C"
{
    // Thread Control Block
    class TCB{
        public:
        TVMMemorySize memorySize;
        TVMStatus status;
        TVMTick tick;
        TVMThreadID threadID;
        TVMThreadPriority priority;
        TVMThreadState state;

    };

    //When a thread is ready, it should be added to the queues
    queue <TCB> ReadyThreadList;

    //May have a list of waiting threads, those that are waiting on sleep, or for a file operation


    // TVMStatus VMStart(int tickms, int argc, char *argv[]);
    // typedef void (*TVMMainEntry)(int, char *[]);
    TVMMainEntry VMLoadModule(const char *module);
    // void MachineInitialize(void);
    // void MachineEnableSignals(void);
    // void MachineTerminate(void);
    void VMUnloadModule(void);
    // TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
    // typedef void (*TMachineFileCallback)(void *calldata, int result);

    //should start with VMStart and VMFileWrite
    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {
        //Create an idle thread, when all other thread are blocked
        TCB* idleThread = new TCB();
        idleThread->state = VM_THREAD_STATE_READY;
        idleThread->priority = VM_THREAD_PRIORITY_LOW;
        idleThread->threadID = 0;



        // 1.load the module with VMLoad that is specifiec by argv[0]
        TVMMainEntry mainEntry = VMLoadModule(argv[0]);

        // 2. Initialize the machine with MachineInitialize
        MachineInitialize();

        // 3. Enable signials with MachineEnablesSignals
        MachineEnableSignals();

        // 4. Call the VMMain entry point
        if (mainEntry)
            mainEntry(argc, argv);
        else
            return VM_STATUS_FAILURE;

        // 5. Terminate the machine with MachineTerminate
        MachineTerminate();

        // 6. Unload the module with VMUnloadModule
        VMUnloadModule();

        delete idleThread;

        // 7. Return form VMStart
        return VM_STATUS_SUCCESS;
    }

    // callback function for MachineFileWrite
    void writeCallback(void *calldata, int result)
    {
    }

    /*
    Description:
    VMFileWrite() attempts to write the number of bytes specified in the integer referenced by
    length from the location specified by data to the file specified by filedescriptor. The
    filedescriptor should have been obtained by a previous call to VMFileOpen(). The actual number
    of bytes transferred by the write will be updated in the length location. When a thread calls
    VMFileWrite() it blocks in the wait state VM_THREAD_STATE_WAITING until the either
    successful or unsuccessful writing of the file is completed.
    */

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
    {
        if (!data || !length)
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        TMachineFileCallback callback = writeCallback;
        MachineFileWrite(filedescriptor, data, *length, callback, NULL);
        return VM_STATUS_SUCCESS;
    }


    /*
    Description
    VMThreadSleep() puts the currently running thread to sleep for tick ticks. If tick is specified as
    VM_TIMEOUT_IMMEDIATE the current process yields the remainder of its processing
    quantum to the next ready process of equal priority.
    */
   // functions that I might need to use: MachineRequestAlarm 
   // I also need an IDLE thread if all threads were to be sleeping 
   // need to change the state of the current thread
    TVMStatus VMThreadSleep(TVMTick tick)
    {
        if (tick == VM_TIMEOUT_INFINITE) return VM_STATUS_ERROR_INVALID_PARAMETER;

        return VM_STATUS_SUCCESS;
    }
}