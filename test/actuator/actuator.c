#include "legato.h"
#include "interfaces.h"

//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "counter" value.
 */
//--------------------------------------------------------------------------------------------------
static void JsonCounterUpdateHandler
(
    double timestamp,
    const char* value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("counter = %s (timestamped %lf)", value, timestamp);
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "counter" value.
 */
//--------------------------------------------------------------------------------------------------
static void NumericCounterUpdateHandler
(
    double timestamp,
    double value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("counter = %lf (timestamped %lf)", value, timestamp);

    // Every 5th push, do some additional testing.
    if (fmod(value, 5.0) == 0)
    {
        LE_INFO("Running create/delete tests");

        // Create the Output with different type and units and expect LE_DUPLICATE.
        le_result_t result = io_CreateOutput("counter", IO_DATA_TYPE_STRING, "count");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateOutput("counter", IO_DATA_TYPE_NUMERIC, "s");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateInput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_DUPLICATE);

        // Create the Output with same type and units and expect LE_OK.
        result = io_CreateOutput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);

        // Delete the Input and re-create it.
        io_DeleteResource("counter");
        result = io_CreateOutput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);
        io_AddNumericPushHandler("counter", NumericCounterUpdateHandler, NULL);
        io_JsonPushHandlerRef_t jsonHandlerRef = io_AddJsonPushHandler("counter",
                                                                       JsonCounterUpdateHandler,
                                                                       NULL);
        io_RemoveJsonPushHandler(jsonHandlerRef);
        io_AddJsonPushHandler("counter", JsonCounterUpdateHandler, NULL);
    }
}


COMPONENT_INIT
{
    le_result_t result;

    // This will be received from the Data Hub.
    result = io_CreateOutput("counter", IO_DATA_TYPE_NUMERIC, "count");
    LE_ASSERT(result == LE_OK);

    // Register for notification of updates to the counter value.
    io_AddNumericPushHandler("counter", NumericCounterUpdateHandler, NULL);
    io_AddJsonPushHandler("counter", JsonCounterUpdateHandler, NULL);
    io_AddStringPushHandler("counter", JsonCounterUpdateHandler, NULL);
}
