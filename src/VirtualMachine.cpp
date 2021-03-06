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
        TVMThreadPriority priority;
        TVMThreadState state;
        TVMThreadEntry entry;
        SMachineContext context;
        SMachineContextRef contextRef;
        TVMThreadIDRef threadIDRef;
        TVMThreadID threadID;
        void *param;
        void *stackAddr;
        string threadName;
        int sleep;
        int result;
        TVMTick mutexTick;
    };

    class Mutex
    {
    public:
        // 1:unlocked , 0:locked
        int state;
        TVMMutexID mutexID;
        TVMMutexIDRef mutexRef;
        TCB *owner;
        // deque<TCB *> waiting_list;
        deque<TCB *> high_waiting_list;
        deque<TCB *> normal_waiting_list;
        deque<TCB *> low_waiting_list;
    };

    vector<Mutex *> mutexes;

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
    TMachineSignalState sigstate;
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

        currentThread->state = VM_THREAD_STATE_READY;

        // cout << "Global_tick: " << Global_tick <<"theadID: "<<currentThread->threadID <<endl;
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
                }
            }
        }

        //Mutex tick
        for (int i = 0; i < mutexes.size(); i++)
        {
            if (!mutexes[i]->high_waiting_list.empty())
            {
                for (int j = 0; j < mutexes[i]->high_waiting_list.size(); j++)
                {
                    TCB *i_thread = mutexes[i]->high_waiting_list.front();
                    if (i_thread->mutexTick != -9999)
                    {
                        i_thread->mutexTick--;
                        if (i_thread->mutexTick == 0)
                        {
                            mutexes[i]->high_waiting_list.pop_front();
                            i_thread->state = VM_THREAD_STATE_READY;
                            high_queue.push_back(i_thread);
                        }
                    }
                }
            }

            if (!mutexes[i]->normal_waiting_list.empty())
            {
                for (int j = 0; j < mutexes[i]->normal_waiting_list.size(); j++)
                {
                    TCB *i_thread = mutexes[i]->normal_waiting_list.front();
                    if (i_thread->mutexTick != -9999)
                    {
                        i_thread->mutexTick--;
                        if (i_thread->mutexTick == 0)
                        {
                            mutexes[i]->normal_waiting_list.pop_front();
                            i_thread->state = VM_THREAD_STATE_READY;
                            normal_queue.push_back(i_thread);
                        }
                    }
                }
            }

            if (!mutexes[i]->low_waiting_list.empty())
            {
                for (int j = 0; j < mutexes[i]->low_waiting_list.size(); j++)
                {
                    TCB *i_thread = mutexes[i]->low_waiting_list.front();
                    if (i_thread->mutexTick != -9999)
                    {
                        i_thread->mutexTick--;
                        if (i_thread->mutexTick == 0)
                        {
                            mutexes[i]->low_waiting_list.pop_front();
                            i_thread->state = VM_THREAD_STATE_READY;
                            low_queue.push_back(i_thread);
                        }
                    }
                }
            }
        }

        scheduler();
    }

    // use this funciton to call the threads entry and ThreadTerminate in case the
    // thread returns from its entry function
    void skeleton(void *param)
    {
        MachineEnableSignals();
        TCB *thread = (TCB *)param;
        thread->entry(thread->param);
        VMThreadTerminate(thread->threadID);
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
            mainThread->threadID = 0;
            mainThread->threadIDRef = &mainThread->threadID;
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
            idleThread->threadID = 1;
            idleThread->threadIDRef = &idleThread->threadID;
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
            // std::cout << "allThread size: " << allThread.size() << "\n";

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
        MachineSuspendSignals(&sigstate);
        if (!entry || !tid)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        TCB *newThread = new TCB;
        newThread->threadIDRef = tid;
        *(newThread->threadIDRef) = (TVMThreadID)allThread.size();
        newThread->state = VM_THREAD_STATE_DEAD;
        newThread->priority = prio;
        newThread->memorySize = memsize;
        newThread->entry = entry;
        newThread->param = param;
        newThread->tick = 0;
        newThread->threadName = "Newthread";
        newThread->contextRef = new SMachineContext;
        newThread->threadID = (TVMThreadID)allThread.size();

        // std::cout << "allThread size: " << allThread.size() << "\n";
        // cout << "ThreadID: " << *(newThread->threadID) << endl;

        allThread.push_back(newThread);

        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateRef)
    {
        // MachineSuspendSignals(&sigstate);
        // if (!thread)
        // {
        //     MachineResumeSignals(&sigstate);
        //     return VM_STATUS_ERROR_INVALID_ID;
        // }
        // if (!stateRef)
        // {
        //     MachineResumeSignals(&sigstate);
        //     return VM_STATUS_ERROR_INVALID_PARAMETER;
        // }

        // *stateRef = allThread[thread]->state;

        // MachineResumeSignals(&sigstate);
        // return VM_STATUS_SUCCESS;
        MachineSuspendSignals(&sigstate);
        // cout<<"VMThreadState()"<<endl;
        // cout<<"Thread ID: "<<thread<<endl;
        // cout<<"allThread.size "<<allThread.size()<<endl;

        if (!stateRef)
        {
            cout << "stateRef == NULL" << endl;
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        if (thread > allThread.size())
        {
            cout << "VM_STATUS_ERROR_INVALID_ID" << endl;
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_ID;
        }

        // for(int i=0;i<allThread.size();i++){
        //     cout<<"loop id: "<<i<<endl;
        // }

        for (int i = 0; i < allThread.size(); i++)
        {
            // cout<<"loop id: "<<i<<endl;
            if (allThread[i]->threadID == thread)
            {
                // cout<<"found id == "<<thread<<endl;
                *stateRef = allThread[i]->state;
                MachineResumeSignals(&sigstate);
                return VM_STATUS_SUCCESS;
            }
        }

        cout << "VM_STATUS_ERROR_INVALID_ID" << endl;
        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }

    TVMStatus VMThreadSleep(TVMTick tick)
    {
        MachineSuspendSignals(&sigstate);

        if (tick == VM_TIMEOUT_INFINITE)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        currentThread->tick = tick;
        currentThread->state = VM_THREAD_STATE_WAITING;
        currentThread->sleep = 1;

        //When this thread is sleeping, we need to schedue other thread
        scheduler();

        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadTerminate(TVMThreadID thread)
    {
        MachineSuspendSignals(&sigstate);

        if (!thread)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_ID;
        }

        // if (allThread[thread]->state != VM_THREAD_STATE_DEAD)
        // { //remove the dead thread from the queue
        //     allThread[thread]->state = VM_THREAD_STATE_DEAD;
        //     if (allThread[thread]->priority == VM_THREAD_PRIORITY_HIGH)
        //     {
        //         for (deque<TCB *>::iterator iter = high_queue.begin(); iter != high_queue.end(); iter++)
        //         {
        //             if ((*iter) == allThread[thread])
        //             {
        //                 high_queue.erase(iter);
        //                 break;
        //             }
        //         }
        //     }
        //     else if (allThread[thread]->priority == VM_THREAD_PRIORITY_NORMAL)
        //     {
        //         for (deque<TCB *>::iterator iter = normal_queue.begin(); iter != normal_queue.end(); iter++)
        //         {
        //             if ((*iter) == allThread[thread])
        //             {
        //                 normal_queue.erase(iter);
        //                 break;
        //             }
        //         }
        //     }
        //     else if (allThread[thread]->priority == VM_THREAD_PRIORITY_LOW)
        //     {
        //         for (deque<TCB *>::iterator iter = low_queue.begin(); iter != low_queue.end(); iter++)
        //         {
        //             if ((*iter) == allThread[thread])
        //             {
        //                 low_queue.erase(iter);
        //                 break;
        //             }
        //         }
        //     }
        // }
        // else
        // {
        //     MachineResumeSignals(&sigstate);
        //     return VM_STATUS_ERROR_INVALID_STATE;
        // }

        // scheduler();

        if (currentThread->threadID == thread)
        {
            currentThread->state = VM_THREAD_STATE_DEAD;
            scheduler();
        }

        for (int i = 0; i < allThread.size(); i++)
        {
            if (allThread[i]->threadID == thread)
            {
                if (allThread[i]->state == VM_THREAD_STATE_DEAD)
                {
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                allThread[i]->state = VM_THREAD_STATE_DEAD;
                MachineResumeSignals(&sigstate);
                return VM_STATUS_SUCCESS;
            }
        }

        MachineResumeSignals(&sigstate);

        return VM_STATUS_ERROR_INVALID_ID;
    }

    TVMStatus VMThreadActivate(TVMThreadID thread)
    {
        MachineSuspendSignals(&sigstate);

        cout << "VMThreadActivate()" << endl;

        TCB *activatingThread = allThread[thread];

        if (activatingThread->state != VM_THREAD_STATE_DEAD)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_STATE;
        }

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
        // if ((currentThread->state == VM_THREAD_STATE_RUNNING && currentThread->priority < activatingThread->priority) || currentThread->state != VM_THREAD_STATE_RUNNING)
        // {
        //     scheduler();
        // }

        if (currentThread->priority < activatingThread->priority)
        {
            scheduler();
        }

        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    void schedule(deque<TCB *> &queue)
    {
        // MachineSuspendSignals(&sigstate);

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
        currentThread = queue.front();
        queue.pop_front();
        currentThread->state = VM_THREAD_STATE_RUNNING;
        MachineContextSwitch(&(old->context), &(currentThread->context));
        MachineResumeSignals(&sigstate);
    }

    // When all other queuq are empty, we schedule the idle thread
    void scheduleIdle()
    {
        // MachineSuspendSignals(sigstate);
        // MachineSuspendSignals(&sigstate);

        // cout << "scheduleIdle()" << endl;

        if (currentThread->state == VM_THREAD_STATE_READY || currentThread->state == VM_THREAD_STATE_RUNNING)
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

        if (currentThread->state == VM_THREAD_STATE_WAITING && currentThread->tick != 0)
        {
            currentThread->sleep = 1;
        }

        TCB *old = currentThread;
        currentThread = globalIdleThread;
        currentThread->state = VM_THREAD_STATE_RUNNING;
        // allThread.push_back(currentThread);
        MachineContextSwitch(&(old->context), &(globalIdleThread->context));
        // MachineResumeSignals(&sigstate);
    }

    // schedule other thread to run
    void scheduler()
    {

        // MachineSuspendSignals(&sigstate);

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

        // MachineResumeSignals(&sigstate);

        return;
    }

    void fileCallback(void *param, int result)
    {

        MachineSuspendSignals(&sigstate);
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
        // if ((currentThread->state == VM_THREAD_STATE_RUNNING && currentThread->priority < thread->priority) || currentThread->state != VM_THREAD_STATE_RUNNING)
        if (currentThread->priority < thread->priority)
        {

            scheduler();
        }

        MachineResumeSignals(&sigstate);
    }

    // Opens and possibly creates a file in the file system
    TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor)
    {
        MachineSuspendSignals(&sigstate);
        if (!filename || !filedescriptor)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        //wait for the operation to complete
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileOpen(filename, flags, mode, fileCallback, currentThread);
        scheduler();
        *filedescriptor = currentThread->result;

        MachineResumeSignals(&sigstate);

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
        MachineSuspendSignals(&sigstate);
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileClose(filedescriptor, fileCallback, currentThread);
        scheduler();

        MachineResumeSignals(&sigstate);

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
        MachineSuspendSignals(&sigstate);
        if (!data || !length)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileRead(filedescriptor, data, *length, fileCallback, currentThread);
        scheduler();
        *length = currentThread->result;

        MachineResumeSignals(&sigstate);

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
        MachineSuspendSignals(&sigstate);

        if (!data || !length)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileWrite(filedescriptor, data, *length, fileCallback, currentThread);
        scheduler();

        MachineResumeSignals(&sigstate);
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

        MachineSuspendSignals(&sigstate);
        currentThread->state = VM_THREAD_STATE_WAITING;
        MachineFileSeek(filedescriptor, offset, whence, fileCallback, currentThread);
        scheduler();
        // MachineResumeSignals(sigstate);
        MachineResumeSignals(&sigstate);

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
        MachineSuspendSignals(&sigstate);
        //cout << "VMTickMS() ID: " << *currentThread->threadID << endl;
        if (!tickmsref)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        *tickmsref = tick_start;

        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMTickCount(TVMTickRef tickref)
    {
        MachineSuspendSignals(&sigstate);

        if (!tickref)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        *tickref = Global_tick;
        // cout<<"VMTickCount() thread--> "<<currentThread<<endl;
        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMThreadID(TVMThreadIDRef threadref)
    {
        MachineSuspendSignals(&sigstate);

        if (threadref == NULL)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        threadref = currentThread->threadIDRef;

        MachineResumeSignals(&sigstate);

        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMMutexCreate(TVMMutexIDRef mutexref)
    {
        MachineSuspendSignals(&sigstate);
        if (!mutexref)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        //once created the mutex is in unlocked state
        Mutex *newMutex = new Mutex;
        //1: unlocked
        newMutex->state = 1;
        newMutex->mutexID = mutexes.size();
        mutexes.push_back(newMutex);
        newMutex->mutexRef = mutexref;
        *mutexref = newMutex->mutexID;

        // cout<<"mutexref"<<*mutexref<<endl;

        MachineResumeSignals(&sigstate);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMMutexDelete(TVMMutexID mutex)
    {
        MachineSuspendSignals(&sigstate);
        for (int i = 0; i < mutexes.size(); i++)
        {
            if (mutexes[i]->mutexID == mutex)
            {
                //if it is locked, then return error
                if (mutexes[i]->state == 0)
                {
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else
                {
                    mutexes.erase(mutexes.begin() + i);
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_SUCCESS;
                }
            }
        }

        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }

    TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref)
    {
        MachineSuspendSignals(&sigstate);

        // cout << "mutex id:" << mutex << endl;
        // cout << "mutex list size" << mutexes.size() << endl;

        if (!ownerref)
        {
            MachineResumeSignals(&sigstate);
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }

        for (int i = 0; i < mutexes.size(); i++)
        {
            // cout << "loop i" << i << endl;
            // cout << "mutexes[i]->mutexID " << mutexes[i]->mutexID << endl;
            if (mutexes[i]->mutexID == mutex)
            {

                if (mutexes[i]->owner)
                {
                    *ownerref = mutexes[i]->owner->threadID;
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_SUCCESS;
                }

                MachineResumeSignals(&sigstate);
                return VM_STATUS_SUCCESS;
            }
        }
        *ownerref = VM_THREAD_ID_INVALID;
        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }

    TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout)
    {
        MachineSuspendSignals(&sigstate);
        // cout<<"VMMutexAcquire() id: "<<mutex<<endl;
        // bool locked = false;
        // for (int i = 0; i < mutexes.size(); i++)
        // {
        //     if (mutexes[i]->state == 0)
        //     {
        //         locked = true;
        //     }
        // }

        for (int i = 0; i < mutexes.size(); i++)
        {
            if (mutexes[i]->mutexID == mutex)
            {
                if (timeout != VM_TIMEOUT_IMMEDIATE && timeout != VM_TIMEOUT_INFINITE)
                {
                    if (mutexes[i]->state == 1)
                    {
                        //no one has the lock
                        //then we can assign the lock
                        mutexes[i]->state = 0;
                        mutexes[i]->owner = currentThread;
                        MachineResumeSignals(&sigstate);
                        return VM_STATUS_SUCCESS;
                    }
                    else
                    {
                        //other thread has the lock
                        //put the current thread into sleep
                        currentThread->mutexTick = timeout;

                        currentThread->state = VM_THREAD_STATE_WAITING;

                        //push the thread into waiting queue
                        if (currentThread->priority == VM_THREAD_PRIORITY_HIGH)
                        {
                            mutexes[i]->high_waiting_list.push_back(currentThread);
                        }
                        else if (currentThread->priority == VM_THREAD_PRIORITY_NORMAL)
                        {
                            mutexes[i]->normal_waiting_list.push_back(currentThread);
                        }
                        else if (currentThread->priority == VM_THREAD_PRIORITY_LOW)
                        {
                            mutexes[i]->low_waiting_list.push_back(currentThread);
                        }

                        scheduler();
                    }
                }
                else if (timeout == VM_TIMEOUT_IMMEDIATE)
                {
                    //the current returns immdiately if the mutex is already locked
                    if (mutexes[i]->state == 0)
                    {
                        MachineResumeSignals(&sigstate);
                        return VM_STATUS_FAILURE;
                    }
                    else
                    {
                        mutexes[i]->state = 0;
                        mutexes[i]->owner = currentThread;
                        MachineResumeSignals(&sigstate);
                        return VM_STATUS_SUCCESS;
                    }
                }
                else if (timeout == VM_TIMEOUT_INFINITE)
                {
                    // cout<<"VM_TIMEOUT_INFINITE"<<endl;
                    //the thread will block until the mutex is acquired.
                    if (mutexes[i]->state == 0)
                    {
                        // cout<<"Cannot get the lock"<<endl;
                        currentThread->mutexTick = -9999;

                        currentThread->state = VM_THREAD_STATE_WAITING;

                        //push the thread into waiting queue
                        if (currentThread->priority == VM_THREAD_PRIORITY_HIGH)
                        {
                            mutexes[i]->high_waiting_list.push_back(currentThread);
                        }
                        else if (currentThread->priority == VM_THREAD_PRIORITY_NORMAL)
                        {
                            mutexes[i]->normal_waiting_list.push_back(currentThread);
                        }
                        else if (currentThread->priority == VM_THREAD_PRIORITY_LOW)
                        {
                            mutexes[i]->low_waiting_list.push_back(currentThread);
                        }

                        scheduler();
                    }
                    else
                    {
                        // cout<<"Got the lock"<<endl;
                        mutexes[i]->state = 0;
                        mutexes[i]->owner = currentThread;
                        MachineResumeSignals(&sigstate);
                        return VM_STATUS_SUCCESS;
                    }
                }
            }
        }

        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    TVMStatus VMMutexRelease(TVMMutexID mutex)
    {
        MachineSuspendSignals(&sigstate);

        for (int i = 0; i < mutexes.size(); i++)
        {
            if (mutexes[i]->mutexID == mutex)
            {
                // if it is not locked, return error
                if (mutexes[i]->state == 1)
                {
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                else
                {
                    //the mutex is locked
                    //release the lock
                    mutexes[i]->state = 1;

                    //find the new holder from the waiting list
                    if (!mutexes[i]->high_waiting_list.empty())
                    {
                        mutexes[i]->owner = mutexes[i]->high_waiting_list.front();
                        mutexes[i]->high_waiting_list.pop_front();
                        mutexes[i]->owner->mutexTick = 0;
                        mutexes[i]->owner->state = VM_THREAD_STATE_READY;
                        high_queue.push_back(mutexes[i]->owner);
                        scheduler();
                    }

                    if (!mutexes[i]->normal_waiting_list.empty())
                    {
                        mutexes[i]->owner = mutexes[i]->normal_waiting_list.front();
                        mutexes[i]->normal_waiting_list.pop_front();
                        mutexes[i]->owner->mutexTick = 0;
                        mutexes[i]->owner->state = VM_THREAD_STATE_READY;
                        normal_queue.push_back(mutexes[i]->owner);
                        scheduler();
                    }

                    if (!mutexes[i]->low_waiting_list.empty())
                    {
                        mutexes[i]->owner = mutexes[i]->low_waiting_list.front();
                        mutexes[i]->low_waiting_list.pop_front();
                        mutexes[i]->owner->mutexTick = 0;
                        mutexes[i]->owner->state = VM_THREAD_STATE_READY;
                        low_queue.push_back(mutexes[i]->owner);
                        scheduler();
                    }

                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_SUCCESS;
                }
            }
        }

        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }

    TVMStatus VMThreadDelete(TVMThreadID thread)
    {
        MachineSuspendSignals(&sigstate);

        for (int i = 0; i < allThread.size(); i++)
        {
            if (allThread[i]->threadID == thread)
            {
                if (allThread[i]->state != VM_THREAD_STATE_DEAD)
                {
                    MachineResumeSignals(&sigstate);
                    return VM_STATUS_ERROR_INVALID_STATE;
                }
                allThread.erase(allThread.begin() + i);
                MachineResumeSignals(&sigstate);
                return VM_STATUS_SUCCESS;
            }
        }

        MachineResumeSignals(&sigstate);
        return VM_STATUS_ERROR_INVALID_ID;
    }
}