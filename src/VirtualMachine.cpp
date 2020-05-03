#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
#include <queue>
#include <vector>

using namespace std;

extern "C"
{
    // Thread Control Block
    class TCB
    {
    public:
        TVMMemorySize memorySize;
        TVMStatus status;
        TVMTick tick;
        TVMThreadID threadID;
        TVMThreadPriority priority;
        TVMThreadState state;
        TVMThreadEntry entry;
        SMachineContext context;
        TVMThreadIDRef tid;
        void  *param;
    };

    class AllThreadInfo
    {
    public:
        vector<TCB*> allThreadList;
        vector<TCB*> readyThread;
    } allThreadInfo;

    // keep track the current number of thread
    volatile int threadNum = 0;

    // **************Globle variable **************
    volatile int Globle_tick;

    //When a thread is ready, it should be added to the queues
    queue<TCB> ReadyThreadList;

    //May have a list of waiting threads, those that are waiting on sleep, or for a file operation

    TVMStatus VMStart(int tickms, int argc, char *argv[]);
    TVMMainEntry VMLoadModule(const char *module);
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    void MachineTerminate(void);
    void VMUnloadModule(void);
    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
    typedef void (*TMachineFileCallback)(void *calldata, int result);
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid);
    void scheduler();
    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state);
    TVMStatus VMThreadActivate(TVMThreadID thread);

    //need a skeleton function to be the initial entry point for the thread

    // Callback function for the MachineRequestAlarm
    void alarmCallback(void *calldata)
    {
        Globle_tick--;
        cout << Globle_tick << endl;
    }

    //should start with VMStart and VMFileWrite
    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {
        cout << "********VMStart********" << endl;

        // TCB for the main thread
        TCB *mainThread = new TCB();
        mainThread->threadID = threadNum++;
        mainThread->priority = VM_THREAD_PRIORITY_NORMAL;
        mainThread->state = VM_THREAD_STATE_RUNNING;
        mainThread->memorySize = 0;
        allThreadInfo.allThreadList.push_back(mainThread);

        //Create an idle thread, when all other thread are blocked
        TCB *idleThread = new TCB();
        idleThread->state = VM_THREAD_STATE_READY;
        idleThread->priority = VM_THREAD_PRIORITY_LOW;
        idleThread->threadID = threadNum++;
        allThreadInfo.allThreadList.push_back(idleThread);

        // 1.load the module with VMLoad that is specifiec by argv[0]
        TVMMainEntry mainEntry = VMLoadModule(argv[0]);

        // 2. Initialize the machine with MachineInitialize
        MachineInitialize();

        TMachineAlarmCallback callback = alarmCallback;
        MachineRequestAlarm(tickms * 1000, callback, NULL);

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

    // It creates a thread in the VM.
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid)
    {
        if (!entry || !tid)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        TCB *thread = new TCB;
        thread->entry = entry;
        thread->param = param;
        thread->memorySize = memsize;
        thread->priority = prio;
        thread->param = param;
        // thread->threadID = *tid++;
        thread->threadID = threadNum++;

        return VM_STATUS_SUCCESS;
    }

    /* description
    VMThreadState() retrieves the state of the thread specified by thread and places the state in the location specified by state.
    */
    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state)
    {

        if (!thread)
            return VM_STATUS_ERROR_INVALID_ID;
        if (!state)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        //there should be a data sturcture to hold all thread, and we need to iterate the list
        //all threads are stored in allThreadInfo.
        for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        {
            if (allThreadInfo.allThreadList[i]->threadID == thread)
            {
                *state = allThreadInfo.allThreadList[i]->state;
            }
        }

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
        if (tick == VM_TIMEOUT_INFINITE)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        Globle_tick = tick;
        while (Globle_tick != 0)
            ;

        cout << Globle_tick;

        return VM_STATUS_SUCCESS;
    }

    //MachineContextCreate() create a context that will enter here
    void entry(void*){
        cout<<"entry";
    }


    /*
    Description:
    VMThreadActivate() activates the dead thread specified by thread parameter in the virtual machine. 
    After activation the thread enters the ready state VM_THREAD_STATE_READY, and must begin at the entry function specified.
    */

    TVMStatus VMThreadActivate(TVMThreadID thread)
    {
        bool found = false;
        for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        {
            if (allThreadInfo.allThreadList[i]->threadID == thread)
            {
                found = true;
                if (allThreadInfo.allThreadList[i]->state != VM_THREAD_STATE_DEAD)
                    return VM_STATUS_ERROR_INVALID_STATE;
                // after avtivation, the state of the thread will become ready
                allThreadInfo.allThreadList[i]->state = VM_THREAD_STATE_READY;
                // initialize the SMachineContext
                SMachineContextRef mcntxref = new SMachineContext;

                /*
                Description:
                    MachineContextCreate() creates a context that will enter in the function specified by entry
                     and passing it the parameter param. The contexts stack of size stacksize must be specified 
                     by the stackaddr parameter. The newly created context will be stored in the mcntxref parameter, 
                     this context can be used in subsequent calls to MachineContextRestore(), or MachineContextSwitch().
                */
                void* stackaddr = (void *)malloc(allThreadInfo.allThreadList[i]->memorySize);
                MachineContextCreate(mcntxref,
                                          entry, allThreadInfo.allThreadList[i]->param, stackaddr, allThreadInfo.allThreadList[i]->memorySize);
                allThreadInfo.allThreadList[i]->context = *mcntxref;
                allThreadInfo.readyThread.push_back(allThreadInfo.allThreadList[i]);
            
            }
        }

        if (!found)
            return VM_STATUS_ERROR_INVALID_ID;
        return VM_STATUS_SUCCESS;
    }

    void scheduler()
    {
    }
}