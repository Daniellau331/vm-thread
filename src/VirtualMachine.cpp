#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
#include <deque>
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
        // TVMThreadID threadID;
        TVMThreadPriority priority;
        TVMThreadState state;
        TVMThreadEntry entry;
        SMachineContext context;
        TVMThreadIDRef threadID;
        void *param;
        void *stackAddr;
        string threadName;
        int sleep;
    };

    // class AllThreadInfo
    // {
    // public:
    //     // TCB* cur_thread;
    //     TVMThreadID CurThreadID;
    //     vector<TCB *> allThreadList;
    //     queue<TCB *> readyThread;
    // } allThreadInfo;

    //***************Global Variable//***************
    volatile int threadNum ;
    volatile int Globle_tick;
    deque<TCB *> high_queue;
    deque<TCB *> normal_queue;
    deque<TCB *> low_queue;
    TCB *currentThread;
    TCB *globalIdleThread;
    TVMThreadID currentThreadID;
    deque<TCB> ReadyThreadList;
    // TVMThreadID mainThreadID = 0;
    // TVMThreadID idleThreadID = 1;
    // TVMThreadIDRef mainThreadRef = &mainThreadID;
    // TVMThreadIDRef idleThreadRef = &idleThreadID;
    TMachineSignalStateRef sigstate;
    vector<TCB *> allThread;

    //Function declaration
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    void scheduler();
    void printThreadList();
    void alarmCallback(void *calldata);
    void skeleton(void *param);
    void printALLQueues();

    // Callback function for the MachineRequestAlarm
    void alarmCallback(void *calldata)
    {
        for (int i = 0; i < allThread.size(); i++)
        {
            //sleep = 1 means sleeping thread
            if (allThread[i]->sleep == 1)
            {
                allThread[i]->tick--;
                if (allThread[i]->tick == 0)
                {
                    allThread[i]->state = VM_THREAD_STATE_READY;
                    // put the thread to the queue
                    if (allThread[i]->priority == VM_THREAD_PRIORITY_HIGH)
                    {
                        high_queue.push_back(allThread[i]);
                    }
                    else if (allThread[i]->priority == VM_THREAD_PRIORITY_NORMAL)
                    {
                        normal_queue.push_back(allThread[i]);
                    }
                    else if (allThread[i]->priority == VM_THREAD_PRIORITY_LOW)
                    {
                        low_queue.push_back(allThread[i]);
                    }
                    //the thread need to wake up
                    allThread[i]->sleep = 0;
                    scheduler();
                }
            }
        }
    }

    // use this funciton to call the threads entry and ThreadTerminate in case the
    // thread returns from its entry function

    void skeleton(void *param)
    {
        MachineEnableSignals();
        cout<<"Skeleton"<<endl;
        TCB *thread = (TCB *)param;
        thread->entry(thread->param);
        //program will be aborted if there is not thread termination
        VMThreadTerminate(*(thread->threadID));
    }

    void idleEntry(void *param)
    {
        while (1)
            ;
    }

    /*
    Descritption:
    VMStart() starts the virtual machine by loading the module specified by argv[0]. 
    The argc and argv are passed directly into the VMMain() function that exists in 
    the loaded module. The time in milliseconds of the virtual machine tick is specified by the tickms parameter.
    */
    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {

        TVMMainEntry mainEntry = VMLoadModule(argv[0]);

        if (mainEntry)
        {

            MachineInitialize();
            MachineRequestAlarm(tickms * 1000, alarmCallback, NULL);
            MachineEnableSignals();

            //create main thread
            TCB *mainThread = new TCB;
            mainThread->threadID = (TVMThreadIDRef)0;
            threadNum++;
            mainThread->state = VM_THREAD_STATE_RUNNING;
            mainThread->priority = VM_THREAD_PRIORITY_NORMAL;
            mainThread->memorySize = 0;
            mainThread->entry = NULL;
            mainThread->param = NULL;
            mainThread->tick = 0;
            mainThread->threadName = "main_Thread";
            allThread.push_back(mainThread);
            currentThread = mainThread;

            //Create idle thread
            TCB *idleThread = new TCB;
            idleThread->threadID = (TVMThreadIDRef)1;
            threadNum++;
            idleThread->state = VM_THREAD_STATE_READY;
            //don't want the idle thread enter those three queues
            idleThread->priority = (int)0x00;
            idleThread->memorySize = 0x100000;
            idleThread->entry = NULL;
            idleThread->param = NULL;
            idleThread->tick = 0;
            idleThread->stackAddr = new char[idleThread->memorySize];
            idleThread->threadName = "idle";
            allThread.push_back(idleThread);

            MachineContextCreate(&(idleThread)->context, idleEntry, NULL, idleThread->stackAddr, idleThread->memorySize);
            globalIdleThread = idleThread;
            mainEntry(argc, argv);
            MachineTerminate();
            VMUnloadModule();
        }

        else
            return VM_STATUS_FAILURE;

        return VM_STATUS_SUCCESS;
    }

    // It creates a thread in the VM.
    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid)
    {
        MachineSuspendSignals(sigstate);
        if (!entry || !tid)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        TCB *newThread = new TCB;
        newThread->threadID = tid;
        newThread->state = VM_THREAD_STATE_DEAD;
        newThread->priority = prio;
        newThread->memorySize = memsize;
        newThread->entry = entry;
        newThread->param = param;
        newThread->tick = 0;
        newThread->threadName = "Newthread";

        
        // *(newThread->threadID) = (TVMThreadID)allThread.size();
        *(newThread->threadID) = threadNum++;
        // cout<<"VMThreadCreate  id: "<<*(newThread->threadID)<<endl;
        // cout<<"size of allThread before push (create) "<<allThread.size()<<endl;
        // cout<<"ThreadID: "<< *(newThread->threadID)<<endl;
        allThread.push_back(newThread);
        // cout<<"size of allThread after push (create) "<<allThread.size()<<endl;
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    /* description
    VMThreadState() retrieves the state of the thread specified by thread and places the state in the location specified by state.
    */
    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateRef)
    {
        MachineSuspendSignals(sigstate);
        // cout<<"VMThreadState-------threadID "<<thread<<endl;
        if (!thread)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        if (!stateRef)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        // cout<<"size of allthread "<<allThread.size()<<endl;
        *stateRef = allThread[thread]->state;
        // cout<<*stateRef<<endl;
        MachineResumeSignals(sigstate);

        return VM_STATUS_SUCCESS;

        // for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        // {
        //     if (allThreadInfo.allThreadList[i]->threadID == thread)
        //     {
        //         *state = allThreadInfo.allThreadList[i]->state;
        //     }
        // }

        // return VM_STATUS_SUCCESS;
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
        MachineSuspendSignals(sigstate);

        if (tick == VM_TIMEOUT_INFINITE)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        currentThread->tick = tick;
        currentThread->state = VM_THREAD_STATE_WAITING;

        // allThreadInfo.allThreadList[allThreadInfo.CurThreadID]->tick = tick;
        // allThreadInfo.allThreadList[allThreadInfo.CurThreadID]->state = VM_THREAD_STATE_WAITING;
        //When this thread is sleeping, we need to schedue other thread
        scheduler();
        MachineResumeSignals(sigstate);
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
        MachineSuspendSignals(sigstate);
        // bool found = false;

        if (!thread || allThread[thread]->state == VM_THREAD_STATE_DEAD)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_ID;
        }

        allThread[thread]->state = VM_THREAD_STATE_DEAD;
        //remove the dead thread from the queue
        if (allThread[thread]->priority == VM_THREAD_PRIORITY_HIGH)
        {
            for (deque<TCB *>::iterator iter = high_queue.begin(); iter != high_queue.end(); iter++)
            {
                if ((*iter) == allThread[thread])
                {
                    high_queue.erase(iter);
                    break;
                }
            }
        }
        else if (allThread[thread]->priority == VM_THREAD_PRIORITY_NORMAL)
        {
            for (deque<TCB *>::iterator iter = normal_queue.begin(); iter != normal_queue.end(); iter++)
            {
                if ((*iter) == allThread[thread])
                {
                    normal_queue.erase(iter);
                    break;
                }
            }
        }
        else if (allThread[thread]->priority == VM_THREAD_PRIORITY_LOW)
        {
            for (deque<TCB *>::iterator iter = low_queue.begin(); iter != low_queue.end(); iter++)
            {
                if ((*iter) == allThread[thread])
                {
                    low_queue.erase(iter);
                    break;
                }
            }
        }

        scheduler();
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;

        // for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        // {
        //     if (thread == allThreadInfo.allThreadList[i]->threadID)
        //     {
        //         found = true;
        //         if (allThreadInfo.allThreadList[i]->state == VM_THREAD_STATE_DEAD)
        //             return VM_STATUS_ERROR_INVALID_STATE;
        //         allThreadInfo.allThreadList[i]->state = VM_THREAD_STATE_DEAD;
        //         //The termination of a thread can trigger another thread to be scheduled.
        //         scheduler();
        //     }
        // }
        // if (!found)
        //     return VM_STATUS_ERROR_INVALID_ID;
        // return VM_STATUS_SUCCESS;
    }

    /*
    Description:
    VMThreadActivate() activates the dead thread specified by thread parameter in the virtual machine. 
    After activation the thread enters the ready state VM_THREAD_STATE_READY, and must begin at the entry function specified.
    */

    TVMStatus VMThreadActivate(TVMThreadID thread)
    {
        MachineSuspendSignals(sigstate);
        // cout<<"VMThreadActivate id: "<<thread<<endl;
        // cout<<"size of the allthead "<<allThread.size()<<endl;

        TCB *activatingThread = allThread[thread];
        cout<<"*activatingThread id: "<<activatingThread->threadName<<endl;
        

        if (activatingThread->state != VM_THREAD_STATE_DEAD)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_STATE;
        }
        // cout<<"VMThreadActivate name: "<<thread<<endl;

        // activating the thread
        activatingThread->state = VM_THREAD_STATE_READY;
        activatingThread->stackAddr =  new char[activatingThread->memorySize];
        MachineContextCreate(&(activatingThread->context), skeleton, activatingThread, activatingThread->stackAddr, activatingThread->memorySize);
        if (activatingThread->priority == VM_THREAD_PRIORITY_HIGH)
        {
            high_queue.push_back(activatingThread);
        }
        else if (activatingThread->priority == VM_THREAD_PRIORITY_NORMAL)
        {
            normal_queue.push_back(activatingThread);
        }
        else if (activatingThread->priority == VM_THREAD_PRIORITY_LOW)
        {
            low_queue.push_back(activatingThread);
        }
        // cout<<"can i get here"<<endl;
        //if the current thread is running and its priority is higher than the activating thread, we don't want to activate it right away
        if ((currentThread->state == VM_THREAD_STATE_RUNNING && currentThread->priority < activatingThread->priority) || currentThread->state != VM_THREAD_STATE_RUNNING)
        scheduler();
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;

        // bool found = false;
        // SMachineContext context;
        // MachineContextCreate(&context, skeleton, NULL, NULL, NULL);

        // for (int i = 0; i < allThreadInfo.allThreadList.size(); i++)
        // {
        //     if (allThreadInfo.allThreadList[i]->threadID == thread)
        //     {
        //         found = true;
        //         TCB *thread = allThreadInfo.allThreadList[i];
        //         if (thread->state != VM_THREAD_STATE_DEAD)
        //             return VM_STATUS_ERROR_INVALID_STATE;

        //         thread->stackAddr = (void *)malloc(thread->memorySize);
        //         cout << "MCC() - id: " << thread->threadID << endl;
        //         MachineContextCreate(&(thread)->context, skeleton, thread, thread->stackAddr, thread->memorySize);
        //         allThreadInfo.allThreadList[i]->state = VM_THREAD_STATE_READY;
        //         allThreadInfo.readyThread.push(thread);
        //     }
        // }
        // MachineResumeSignals(sigstate);
        // if (!found)
        // return VM_STATUS_ERROR_INVALID_ID;
    }

    void schedule(deque<TCB *> &queue)
    {   
        // cout<<"schedule"<<endl;
        // if the current thread is running or ready, they need to beomce ready and be pushed back to its queue
        if (currentThread->state == VM_THREAD_STATE_RUNNING || currentThread->state == VM_THREAD_STATE_READY)
        {
            // cout<<"currentThread->state == VM_THREAD_STATE_RUNNING || currentThread->state == VM_THREAD_STATE_READY"<<endl;
            currentThread->state = VM_THREAD_STATE_READY;
            if (currentThread->priority == VM_THREAD_PRIORITY_HIGH)
            {
                high_queue.push_back(currentThread);
            }
            else if (currentThread->priority == VM_THREAD_PRIORITY_NORMAL)
            {
                normal_queue.push_back(currentThread);
            }
            else if (currentThread->priority == VM_THREAD_PRIORITY_LOW)
            {
                low_queue.push_back(currentThread);
            }
        }
        // printALLQueues();
        if (currentThread->state == VM_THREAD_STATE_WAITING && currentThread->tick != 0)
        {
            currentThread->sleep = 1;
        }

        TCB *old = currentThread;
        //get the fist item in the queue
        currentThread = queue.front();
        queue.pop_front();
        currentThread->state = VM_THREAD_STATE_RUNNING;
        MachineContextSwitch(&old->context, &currentThread->context);
    }

    // When all other queuq are empty, we schedule the idle thread
    void scheduleIdle()
    {
        if (currentThread->state == VM_THREAD_STATE_RUNNING || currentThread->state == VM_THREAD_STATE_READY)
        {
            currentThread->state = VM_THREAD_STATE_READY;
            if (currentThread->priority == VM_THREAD_PRIORITY_HIGH)
            {
                high_queue.push_back(currentThread);
            }
            else if (currentThread->priority == VM_THREAD_PRIORITY_NORMAL)
            {
                normal_queue.push_back(currentThread);
            }
            else if (currentThread->priority == VM_THREAD_PRIORITY_LOW)
            {
                low_queue.push_back(currentThread);
            }
        }
        //waiting does not mean sleeping
        if (currentThread->state == VM_THREAD_STATE_WAITING && currentThread->tick != 0)
        {
            currentThread->sleep = 1;
        }

        TCB *old = currentThread;
        currentThread = globalIdleThread;
        currentThread->state = VM_THREAD_STATE_RUNNING;
        allThread.push_back(currentThread);
        MachineContextSwitch(&old->context, &currentThread->context);
    }

    // schedule other thread to run
    void scheduler()
    {
        // cout<<"Scheduler()"<<endl;
        if (!high_queue.empty())
        {
            // cout<<"schedule high"<<endl;
            schedule(high_queue);
        }
        else if (!normal_queue.empty())
        {
            // cout<<"schedule normal"<<endl;

            schedule(normal_queue);
        }
        else if (!low_queue.empty())
        {
            // cout<<"schedule low"<<endl;

            schedule(low_queue);
        }
        else
        {
            // cout<<"schedule idle"<<endl;

            //run the idle
            scheduleIdle();
        }
    

        // //old machine context is the current thread's machine context
        // SMachineContextRef mcntxold = &(allThreadInfo.allThreadList[allThreadInfo.CurThreadID]->context);
        // cout << "The current thread is " << allThreadInfo.allThreadList[allThreadInfo.CurThreadID]->threadName << endl;
        // //Get the new machine context from the ready list
        // SMachineContextRef mcntxnew = &(allThreadInfo.readyThread.back()->context);
        // cout << "New thread is " << allThreadInfo.readyThread.back()->threadName << endl;
        // allThreadInfo.CurThreadID = allThreadInfo.readyThread.back()->threadID;

        // // allThreadInfo.readyThread.push(allThreadInfo.allThreadList[allThreadInfo.CurThreadID]);
        // MachineContextSwitch(mcntxold, mcntxnew);
    }

    void printALLQueues(){
        cout<<"PrintAllQueues"<<endl;
        cout<<high_queue.size()<<endl;
        cout<<normal_queue.size()<<endl;
        cout<<low_queue.size()<<endl;
        
        // for(int i=0;i<high_queue.size();i++){
        //     cout<<*high_queue[i]->threadID;
        // }
        // cout<<"normal_queue"<<endl;
        // for(int i=0;i<normal_queue.size();i++){
        //     cout<<*normal_queue[i]->threadID;
        // }
        // cout<<"low_queue"<<endl;
        // for(int i=0;i<low_queue.size();i++){
        //     cout<<*low_queue[i]->threadID;
        // }
    }

    // void printThreadList()
    // {
    //     for (auto item : allThreadInfo.allThreadList)
    //     {
    //         cout << "Thread ID: " << item->threadID << " state: " << item->state << endl;
    //     }
    // }
}