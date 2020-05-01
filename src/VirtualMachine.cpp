#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
using namespace std;

extern "C"
{

    typedef void (*TVMMainEntry)(int, char *[]);
    TVMMainEntry VMLoadModule(const char *module);
    void MachineInitialize(void);
    void MachineEnableSignals(void);
    void MachineTerminate(void);
    void VMUnloadModule(void);

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
        if(mainEntry)
            mainEntry(argc,argv);
        else
            return VM_STATUS_FAILURE;

        // 5. Terminate the machine with MachineTerminate
        MachineTerminate();

        // 6. Unload the module with VMUnloadModule
        VMUnloadModule();

        // 7. Return form VMStart
        return VM_STATUS_SUCCESS;
    }

    TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
    {

        return VM_STATUS_SUCCESS;
    }
}