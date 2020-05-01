#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
using namespace std;

extern "C"
{
    TVMStatus VMStart(int tickms, int argc, char *argv[]);
    typedef void (*TVMMainEntry)(int, char *[]);
    TVMMainEntry VMLoadModule(const char *module);
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    void MachineTerminate(void);
    void VMUnloadModule(void);
    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
    typedef void (*TMachineFileCallback)(void *calldata, int result);

    //should start with VMStart and VMFileWrite
    TVMStatus VMStart(int tickms, int argc, char *argv[])
    {

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

        // 7. Return form VMStart
        return VM_STATUS_SUCCESS;
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

   void writeCallback(void* calldata, int result){

   }

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
    {
        TMachineFileCallback callback = writeCallback;
        MachineFileWrite(filedescriptor, data, *length, callback, NULL);
        return VM_STATUS_SUCCESS;
    }
}