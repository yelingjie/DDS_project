/* Compile with ONLY application include paths; no OS or port headers. */
#include "pubsub.h"
#include "pubsub_demo.h"

PsStatus api_header_check(PsClient *client)
{
    PsMessage message;
    return ps_receive(client, &message, PS_WAIT_FOREVER);
}
