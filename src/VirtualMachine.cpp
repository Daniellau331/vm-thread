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
        int result;
    };

    //***************Global Variable//***************
    volatile int threadNum;
    volatile int Global_tick = 0;
    volatile int tick_start;
    deque<TCB *> high_queue;
    deque<TCB *> normal_queue;
    deque<TCB *> low_queue;
    TCB *currentThread;
    TCB *globalIdleThread;
    TVMThreadID currentThreadID;
    deque<TCB> ReadyThreadList;
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
        // cout << "alarmCallback()" << endl;
        Global_tick++;

        currentThread->state =VM_THREAD_STATE_READY;

        cout << "Global_tick: " << Global_tick << endl;
        for (int i = 0; i < allThread.size(); i++)
        {
            //sleep = 1 means sleeping thread
            if (allThread[i]->sleep == 1)
            {
                allThread[i]->tick--;
                if (allThread[i]->tick == 0)
                {
                    //time to wake up
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
        TCB *thread = (TCB *)param;
        thread->entry(thread->param);
        VMThreadTerminate(*(thread->threadID));
    }

    void idleEntry(void *param)
    {

        while (1)
            ;
    }

    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {

        TVMMainEntry mainEntry = VMLoadModule(argv[0]);
        tick_start = tickms;
        // cout << "tick_start: " << tick_start << endl;

        if (mainEntry)
        {
            MachineInitialize();
            MachineRequestAlarm(tickms * 1000, alarmCallback, NULL);
            MachineEnableSignals();

            //create main thread
            TCB *mainThread = new TCB;
            mainThread->threadID = (TVMThreadIDRef)0;
            mainThread->state = VM_THREAD_STATE_RUNNING;
            mainThread->priority = VM_THREAD_PRIORITY_NORMAL;
            mainThread->memorySize = 0;
            mainThread->entry = NULL;
            mainThread->param = NULL;
            mainThread->tick = 0;
            mainThread->threadName = "main_Thread";
            mainThread->stackAddr = new char[mainThread->memorySize];
            allThread.push_back(mainThread);
            currentThread = mainThread;

            //Create idle thread
            TCB *idleThread = new TCB;
            idleThread->threadID = (TVMThreadIDRef)1;
            idleThread->state = VM_THREAD_STATE_READY;
            idleThread->priority = (TVMThreadPriority)0x00;
            idleThread->memorySize = 0x100000;
            idleThread->entry = NULL;
            idleThread->param = NULL;
            idleThread->tick = 0;
            idleThread->stackAddr = new char[idleThread->memorySize];
            idleThread->threadName = "idle";
            // (idleThread->context) = new SMachineContext;
            globalIdleThread = idleThread;
            //allThread.push_back(globalIdleThread);
            std::cout << "allThread size: " << allThread.size() << "\n";

            MachineContextCreate(&(idleThread)->context, idleEntry, NULL, idleThread->stackAddr, idleThread->memorySize);

            mainEntry(argc, argv);
            MachineTerminate();
            VMUnloadModule();
        }

        else
            return VM_STATUS_FAILURE;

        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid)
    {
        // MachineSuspendSignals(sigstate);
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
        newThread->threadID = tid;
        *(newThread->threadID) = (TVMThreadID)allThread.size();
        std::cout << "allThread size: " << allThread.size() << "\n";
        cout << "ThreadID: " << *(newThread->threadID) << endl;
        allThread.push_back(newThread);
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateRef)
    {
        // MachineSuspendSignals(sigstate);
        // cout << "VMThreadState-------threadID " << thread << endl;
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
        *stateRef = allThread[thread]->state;
        MachineResumeSignals(sigstate);
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
        // MachineSuspendSignals(sigstate);

        if (tick == VM_TIMEOUT_INFINITE)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        currentThread->tick = tick;
        currentThread->state = VM_THREAD_STATE_WAITING;

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
        // MachineSuspendSignals(sigstate);
        // bool found = false;

        if (!thread)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_ID;
        }
        if (allThread[thread]->state != VM_THREAD_STATE_DEAD)
        { //remove the dead thread from the queue
            allThread[thread]->state = VM_THREAD_STATE_DEAD;
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
                        std::cout << low_queue.size() << std::endl;
                        low_queue.erase(iter);
                        std::cout << low_queue.size() << std::endl;
                        break;
                    }
                }
            }
        }
        else
        { //invalid state ( dead)
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_STATE;
        }

        scheduler();
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    /*
    Description:
    VMThreadActivate() activates the dead thread specified by thread parameter in the virtual machine. 
    After activation the thread enters the ready state VM_THREAD_STATE_READY, and must begin at the entry function specified.
    */

    TVMStatus VMThreadActivate(TVMThreadID thread)
    {
        // MachineSuspendSignals(sigstate);

        TCB *activatingThread = allThread[thread];
        // cout << "*activatingThread id: " << activatingThread->threadName << endl;

        if (activatingThread->state != VM_THREAD_STATE_DEAD)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_STATE;
        }
        // cout<<"VMThreadActivate name: "<<thread<<endl;

        // activating the thread
        activatingThread->state = VM_THREAD_STATE_READY;
        activatingThread->stackAddr = new char[activatingThread->memorySize];
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
        //if the current thread is running and its priority is higher than the activating thread, we don't want to activate it right away
        if ((currentThread->state == VM_THREAD_STATE_RUNNING && currentThread->priority < activatingThread->priority) || currentThread->state != VM_THREAD_STATE_RUNNING)
        {
            scheduler();
        }

        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    void schedule(deque<TCB *> &queue)
    {
        if (currentThread->tick < 0)
        {
            std::cout << "ERROR IN NEGATIVE TICK\n";
        }
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
        if (currentThread->state == VM_THREAD_STATE_WAITING && currentThread->tick > 0)
        {
            currentThread->sleep = 1;
        }

        TCB *old = currentThread;
        //get the fist item in the queue
        currentThread = queue.front();
        queue.pop_front();
        currentThread->state = VM_THREAD_STATE_RUNNING;
        MachineContextSwitch(&(old->context), &(currentThread->context));
    }

    // When all other queuq are empty, we schedule the idle thread
    void scheduleIdle()
    {
        if (currentThread->state == VM_THREAD_STATE_READY)
        {
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
        MachineContextSwitch(&(old->context), &(globalIdleThread->context));
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
            scheduleIdle();
        }

        return;
    }

    void printALLQueues()
    {
        cout << "PrintAllQueues" << endl;
        cout << high_queue.size() << endl;
        cout << normal_queue.size() << endl;
        cout << low_queue.size() << endl;
    }

    void fileCallback(void *param, int result)
    {
        TCB *thread = (TCB *)param;
        //result is the file descriptor of the newly opened file

        thread->state = VM_THREAD_STATE_READY;
        if (thread->priority == VM_THREAD_PRIORITY_HIGH)
        {
            high_queue.push_back(thread);
        }
        else if (thread->priority == VM_THREAD_PRIORITY_NORMAL)
        {
            normal_queue.push_back(thread);
        }
        else if (thread->priority == VM_THREAD_PRIORITY_LOW)
        {
            low_queue.push_back(thread);
        }

        thread->result = result;
        if ((currentThread->state == VM_THREAD_STATE_RUNNING && currentThread->priority < thread->priority) || currentThread->state != VM_THREAD_STATE_RUNNING)
            scheduler();
    }

    // Opens and possibly creates a file in the file system
    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor)
    {
        // MachineSuspendSignals(sigstate);
        if (!filename || !filedescriptor)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        //wait for the operation to complete
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileOpen(filename, flags, mode, fileCallback, currentThread);
        scheduler();
        *filedescriptor = currentThread->result;
        MachineResumeSignals(sigstate);
        if (*filedescriptor < 0)
        {
            return VM_STATUS_FAILURE;
        }
        else
        {
            return VM_STATUS_SUCCESS;
        }
    }

    TVMStatus VMFileClose(int filedescriptor)
    {
        // MachineSuspendSignals(sigstate);
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileClose(filedescriptor, fileCallback, currentThread);
        scheduler();
        MachineResumeSignals(sigstate);
        if (currentThread->result < 0)
        {
            return VM_STATUS_FAILURE;
        }
        else
        {
            return VM_STATUS_SUCCESS;
        }
    }

    TVMStatus VMFileRead(int filedescriptor, void *data, int *length)
    {
        // MachineSuspendSignals(sigstate);
        if (!data || !length)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileRead(filedescriptor, data, *length, fileCallback, currentThread);
        scheduler();
        *length = currentThread->result;
        MachineResumeSignals(sigstate);
        if (currentThread->result < 0)
        {
            return VM_STATUS_FAILURE;
        }
        else
        {
            return VM_STATUS_SUCCESS;
        }
    }

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
    {
        
        if (!data || !length)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileWrite(filedescriptor, data, *length, fileCallback, currentThread);
        scheduler();
        MachineResumeSignals(sigstate);
        if (currentThread->result < 0)
        {
            return VM_STATUS_FAILURE;
        }
        else
        {
            return VM_STATUS_SUCCESS;
        }
    }

    TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset)
    {

        // MachineSuspendSignals(sigstate);
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, offset, whence, fileCallback, currentThread);
        scheduler();
        MachineResumeSignals(sigstate);

        if (newoffset)
        {
            *newoffset = currentThread->result;
            return VM_STATUS_SUCCESS;
        }
        else
        {
            return VM_STATUS_FAILURE;
        }
    }

    // Retrives milliseconds between ticks of the virtual machine
    TVMStatus VMTickMS(int *tickmsref)
    {
        // MachineSuspendSignals(sigstate);
        //cout << "VMTickMS() ID: " << *currentThread->threadID << endl;
        if (!tickmsref)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        *tickmsref = tick_start;
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMTickCount(TVMTickRef tickref)
    {
        if (!tickref)
        {
            MachineResumeSignals(sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        *tickref = Global_tick;
        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadID(TVMThreadIDRef threadref)
    {
        if (threadref == NULL)
            return VM_STATUS_ERROR_INVALID_PARAMETER;

        // MachineSuspendSignals(sigstate);
        threadref = currentThread->threadID;

        MachineResumeSignals(sigstate);
        return VM_STATUS_SUCCESS;
    }
}