#include "position.h"
#include "uci.h"
#include "magic.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    // CRITICAL: Disable stdout buffering for UCI/Arena compatibility
    // Without this, Arena may not receive responses in time
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    // Initialize magic bitboards
    init_magic_tables();

    // Initialize zobrist hashing
    zobrist_init();

    // Run UCI event loop
    engine_run_uci_loop();

    // Cleanup
    deinit_magic_tables();

    return 0;
}
