#include "legato.h"
#include "interfaces.h"

static bool IsEnabled = false;

static le_timer_Ref_t Timer;

#define COUNTER_NAME "counter/value"
#define PERIOD_NAME "counter/period"
#define ENABLE_NAME "counter/enable"

//--------------------------------------------------------------------------------------------------
/**
 * Function called when the timer expires.
 */
//--------------------------------------------------------------------------------------------------
static void TimerExpired
(
    le_timer_Ref_t timer
)
//--------------------------------------------------------------------------------------------------
{
    static double counter = 0;

    counter++;

    io_PushNumeric(COUNTER_NAME, 0.0, counter);

    // On the 3rd push, do some additional testing.
    if (counter == 3.0)
    {
        LE_INFO("Running create/delete tests");

        // Create the Input with different type and units and expect LE_DUPLICATE.
        le_result_t result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_STRING, "count");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "s");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateOutput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_DUPLICATE);

        // Create the Input with same type and units and expect LE_OK.
        result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);

        // Delete the Input and re-create it.
        io_DeleteResource(COUNTER_NAME);
        result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "period"
 * config setting.
 */
//--------------------------------------------------------------------------------------------------
static void PeriodUpdateHandler
(
    double timestamp,
    double value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("Received update to 'period' setting: %lf (timestamped %lf)", value, timestamp);

    uint32_t ms = (uint32_t)(value * 1000);

    if (ms == 0)
    {
        le_timer_Stop(Timer);
    }
    else
    {
        le_timer_SetMsInterval(Timer, ms);

        // If the sensor is enabled and the timer is not already running, start it now.
        if (IsEnabled && (!le_timer_IsRunning(Timer)))
        {
            le_timer_Start(Timer);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "enable"
 * control.
 */
//--------------------------------------------------------------------------------------------------
static void EnableUpdateHandler
(
    double timestamp,
    bool value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("Received update to 'enable' setting: %s (timestamped %lf)",
            value == false ? "false" : "true",
            timestamp);

    IsEnabled = value;

    if (value)
    {
        // If the timer has a non-zero interval and is not already running, start it now.
        if ((le_timer_GetMsInterval(Timer) != 0) && (!le_timer_IsRunning(Timer)))
        {
            le_timer_Start(Timer);
        }
    }
    else
    {
        le_timer_Stop(Timer);
    }
}


COMPONENT_INIT
{
    le_result_t result;

    // This will be provided to the Data Hub.
    result = io_CreateInput(COUNTER_NAME, IO_DATA_TYPE_NUMERIC, "count");
    LE_ASSERT(result == LE_OK);

    // This is my configuration setting.
    result = io_CreateOutput(PERIOD_NAME, IO_DATA_TYPE_NUMERIC, "s");
    LE_ASSERT(result == LE_OK);

    // Register for notification of updates to our configuration setting.
    io_AddNumericPushHandler(PERIOD_NAME, PeriodUpdateHandler, NULL);

    // This is my enable/disable control.
    result = io_CreateOutput(ENABLE_NAME, IO_DATA_TYPE_BOOLEAN, "");
    LE_ASSERT(result == LE_OK);

    // Register for notification of updates to our enable/disable control.
    io_AddBooleanPushHandler(ENABLE_NAME, EnableUpdateHandler, NULL);

    // Create a repeating timer that will call TimerExpired() each time it expires.
    // Note: we'll start the timer when we receive our configuration setting.
    Timer = le_timer_Create(COUNTER_NAME);
    le_timer_SetRepeat(Timer, 0);
    le_timer_SetHandler(Timer, TimerExpired);

    LE_ASSERT(le_timer_GetMsInterval(Timer) == 0);
}
