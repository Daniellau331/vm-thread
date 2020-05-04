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
        void *param;
        void * stackAddr;
    };

    class AllThreadInfo
    {
    public:
        // TCB* cur_thread;
        TVMThreadID CurThreadID;
        vector<TCB *> allThreadList;
        queue<TCB *> readyThread;
    } allThreadInfo;


    //Global Variable 
    volatile int threadNum;
    volatile int Globle_tick;
    queue<TCB> ReadyThreadList;

    //May have a list of waiting threads, those that are waiting on sleep, or for a file operation

    //Function declaration
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    void scheduler();
    void printThreadList();
    void alarmCallback(void *calldata);
    void skeleton(void* param);


    // Callback function for the MachineRequestAlarm
    void alarmCallback(void *calldata)
    {
        Globle_tick--;
    }

     // use this funciton to call the threads entry and ThreadTerminate in case the
    // thread returns from its entry function
    void skeleton(void* param)
    {
        cout<<"skeleton"<<endl;
        TCB* thread = (TCB*) param;
        thread->entry(thread->param);

        VMThreadTerminate(allThreadInfo.CurThreadID);
    }

    /*
    Descritption:
    VMStart() starts the virtual machine by loading the module specified by argv[0]. 
    The argc and argv are passed directly into the VMMain() function that exists in 
    the loaded module. The time in milliseconds of the virtual machine tick is specified by the tickms parameter.
    */
    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {


        TVMThreadID VMThreadID, IdleThreadID;
        VMThreadCreate(skeleton, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &VMThreadID);
        allThreadInfo.allThreadList[0]->state = VM_THREAD_STATE_RUNNING;
        VMThreadCreate(skeleton,NULL,0x100000,VM_THREAD_PRIORITY_LOW,&IdleThreadID);

   
        TVMMainEntry mainEntry = VMLoadModule(argv[0]);

        MachineInitialize();
        MachineRequestAlarm(tickms * 1000, alarmCallback, NULL);
        MachineEnableSignals();

        if (mainEntry)
            mainEntry(argc, argv);
        else
            return VM_STATUS_FAILURE;

        MachineTerminate();
        VMUnloadModule();
        return VM_STATUS_SUCCESS;
    }

    // It creates a thread in the VM.
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid)
    {
        if (!entry || !tid) return VM_STATUS_ERROR_INVALID_PARAMETER;


        TCB *thread = new TCB;
        thread->threadID = threadNum++;
        thread->entry = entry;
        thread->param = param;
        thread->memorySize = memsize;
        thread->priority = prio;
        thread->param = param;
        thread->state = VM_THREAD_STATE_DEAD;
        *tid = thread->threadID;
        allThreadInfo.allThreadList.push_back(thread);
        // printThreadList();

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

        // Cannot use global varible to keep track of the tick
        // There might be more than two sleeping thread at the same time
        Globle_tick = tick;
        //need to add a loop here, we don't want the code exits before the Globle_tick became 0
        while (Globle_tick != 0);

        //When this thread is sleeping, we need to schedue other thread
        // scheduler();

        return VM_STATUS_SUCCESS;
    }

    /*
    Description:
    VMThreadTerminate() terminates the thread specified by thread parameter in the virtual machine. 
    After termination the thread enters the state VM_THREAD_STATE_DEAD. The termination of a thread 
    can trigger another thread to be scheduled.
    */
    TVMStatus VMThreadTerminate(TVMThreadID thread)
    {
        if (!thread)
            return VM_STATUS_ERROR_INVALID_ID;
        bool found = false;

        for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        {
            if (thread == allThreadInfo.allThreadList[i]->threadID)
            {
                found = true;
                if (allThreadInfo.allThreadList[i]->state == VM_THREAD_STATE_DEAD)
                    return VM_STATUS_ERROR_INVALID_STATE;
                allThreadInfo.allThreadList[i]->state = VM_THREAD_STATE_DEAD;
                //The termination of a thread can trigger another thread to be scheduled.
            }
        }
        if (!found)
            return VM_STATUS_ERROR_INVALID_ID;
        return VM_STATUS_SUCCESS;
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
                TCB* thread = allThreadInfo.allThreadList[i];
                if (thread->state != VM_THREAD_STATE_DEAD) return VM_STATUS_ERROR_INVALID_STATE;


                thread->stackAddr = (void *)malloc(thread->memorySize);
                cout<<"MCC()"<<endl;
                MachineContextCreate(&thread->context,skeleton, thread, thread->stackAddr, allThreadInfo.allThreadList[i]->memorySize);
                allThreadInfo.allThreadList[i]->state = VM_THREAD_STATE_READY;
                allThreadInfo.readyThread.push(thread);
            }
        }

        if (!found)
            return VM_STATUS_ERROR_INVALID_ID;
        return VM_STATUS_SUCCESS;
    }

    // Determine who should run next from the ready queue
    void scheduler()
    {
        SMachineContextRef mcntxold, mcntxnew;
        //old machine context is the current thread's machine context
        mcntxold = &(allThreadInfo.allThreadList[allThreadInfo.CurThreadID]->context);
        //Get the new machine context from the ready list
        mcntxnew = &(allThreadInfo.readyThread.front()->context);
        allThreadInfo.CurThreadID = allThreadInfo.readyThread.front()->threadID;
        MachineContextSwitch(mcntxold, mcntxnew);
    }

    void printThreadList(){
        for(auto item:allThreadInfo.allThreadList){
            cout<<"Thread ID: "<<item->threadID<<" state: "<<item->state<<endl;
        }
    }
}