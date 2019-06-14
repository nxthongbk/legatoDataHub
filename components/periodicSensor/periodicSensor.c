//--------------------------------------------------------------------------------------------------
/**
 * Periodic sensor scaffold.  Used to implement generic periodic sensors.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "periodicSensor.h"


//--------------------------------------------------------------------------------------------------
/**
 * Sensor Scaffold object.
 */
//--------------------------------------------------------------------------------------------------
typedef struct psensor
{
    bool isEnabled;
    double period;  ///< seconds (0.0 = not set yet)
    le_timer_Ref_t timer;
    void (*sampleFunc)(psensor_Ref_t, void *);
    void *sampleFuncContext;
    char name[PSENSOR_MAX_NAME_BYTES];

    dhubIO_TriggerPushHandlerRef_t triggerHandlerRef;
    dhubIO_NumericPushHandlerRef_t periodHandlerRef;
    dhubIO_BooleanPushHandlerRef_t enableHandlerRef;
}
Sensor_t;


//--------------------------------------------------------------------------------------------------
/**
 * Timer expiry handler function.
 */
//--------------------------------------------------------------------------------------------------
static void HandleTimerExpiry
(
    le_timer_Ref_t timer
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = le_timer_GetContextPtr(timer);

    sensorPtr->sampleFunc(sensorPtr, sensorPtr->sampleFuncContext);
}


//--------------------------------------------------------------------------------------------------
/**
 * Handle an "enable" update from the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void HandleEnablePush
(
    double timestamp,   ///< Don't care about this.
    bool enable,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = contextPtr;

    if (sensorPtr->isEnabled != enable)
    {
        sensorPtr->isEnabled = enable;

        if (enable)
        {
            // If the period has been set, start the timer.
            if (sensorPtr->period > 0.0)
            {
                le_timer_Start(sensorPtr->timer);
            }
            else
            {
                LE_WARN("Sensor '%s' enabled before its period has been set.", sensorPtr->name);
            }
        }
        else
        {
            le_timer_Stop(sensorPtr->timer);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Handle a "period" update from the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void HandlePeriodPush
(
    double timestamp,   ///< Don't care about this.
    double period,      ///< seconds
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = contextPtr;

    // If the new value is the same as the old value, ignore the push.
    if (sensorPtr->period != period)
    {
        // Sanity check the period.
        // If it's invalid, stop the timer and set the period to 0.0.
        if (period <= 0.0)
        {
            LE_ERROR("Timer period %lf is out of range. Must be > 0.", period);
            le_timer_Stop(sensorPtr->timer);
            sensorPtr->period = 0.0;
        }
        else if (period > (double)(0x7FFFFFFF)) // Don't know how big time_t is, assume 32-bits.
        {
            LE_ERROR("Timer period %lf is too high.", period);
            le_timer_Stop(sensorPtr->timer);
            sensorPtr->period = 0.0;
        }
        else
        {
            // The new period is good.
            // Set the timer's interval to the period.
            le_clk_Time_t interval;
            interval.sec = (time_t)period;
            interval.usec = (period - interval.sec) * 1000000;
            le_timer_SetInterval(sensorPtr->timer, interval);

            // If the old value was zero and the sensor is enabled, start the timer now.
            if ((sensorPtr->period == 0) && sensorPtr->isEnabled)
            {
                le_timer_Start(sensorPtr->timer);
            }

            sensorPtr->period = period;
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Handle a "trigger" update from the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void HandleTriggerPush
(
    double timestamp,   ///< Don't care about this.
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = contextPtr;

    if (sensorPtr->isEnabled)
    {
        sensorPtr->sampleFunc(sensorPtr, sensorPtr->sampleFuncContext);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Creates a periodic sensor scaffold for a sensor with a given name.
 *
 * This makes the sensor appear in the Data Hub and creates a timer for that sensor.
 * The sampleFunc will be called whenever it's time to take a sample.  The sampleFunc must
 * call one of the psensor_PushX() functions below.
 *
 * @return Reference to the new periodic sensor scaffold.
 */
//--------------------------------------------------------------------------------------------------
psensor_Ref_t psensor_Create
(
    const char* name,   ///< Name of the periodic sensor.
    dhubIO_DataType_t dataType,
    const char* units,
    void (*sampleFunc)(psensor_Ref_t ref,
                       void *context), ///< Sample function to be called back periodically.
    void *sampleFuncContext  ///< Context pointer to be passed to the sample function
)
//--------------------------------------------------------------------------------------------------
{
    // Since there's no way to delete a sensor scaffold, it's okay to use malloc() here.
    Sensor_t* sensorPtr = malloc(sizeof(Sensor_t));
    LE_ASSERT(sensorPtr != NULL);

    sensorPtr->isEnabled = false;
    sensorPtr->period = 0.0;

    sensorPtr->timer = le_timer_Create(name);
    le_timer_SetRepeat(sensorPtr->timer, 0); // Repeat an infinite number of times.
    le_timer_SetHandler(sensorPtr->timer, HandleTimerExpiry);
    le_timer_SetContextPtr(sensorPtr->timer, sensorPtr);

    sensorPtr->sampleFunc = sampleFunc;
    sensorPtr->sampleFuncContext = sampleFuncContext;

    if (le_utf8_Copy(sensorPtr->name, name, sizeof(sensorPtr->name), NULL) != LE_OK)
    {
        LE_FATAL("Sensor name too long (%s)", name);
    }

    // Create the Data Hub resources "value", "enable", "period", and "trigger" for this sensor.
    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];
    LE_ASSERT(snprintf(path, sizeof(path), "%s/value", name) < sizeof(path));
    le_result_t result = dhubIO_CreateInput(path, dataType, units);
    if (result != LE_OK)
    {
        LE_FATAL("Failed to create Data Hub Input '%s' (%s).", path, LE_RESULT_TXT(result));
    }

    LE_ASSERT(snprintf(path, sizeof(path), "%s/enable", name) < sizeof(path));
    result = dhubIO_CreateOutput(path, DHUBIO_DATA_TYPE_BOOLEAN, "");
    if (result != LE_OK)
    {
        LE_FATAL("Failed to create Data Hub Output '%s' (%s).", path, LE_RESULT_TXT(result));
    }
    sensorPtr->enableHandlerRef = dhubIO_AddBooleanPushHandler(path, HandleEnablePush, sensorPtr);

    LE_ASSERT(snprintf(path, sizeof(path), "%s/period", name) < sizeof(path));
    result = dhubIO_CreateOutput(path, DHUBIO_DATA_TYPE_NUMERIC, "s");
    if (result != LE_OK)
    {
        LE_FATAL("Failed to create Data Hub Output '%s' (%s).", path, LE_RESULT_TXT(result));
    }
    sensorPtr->periodHandlerRef = dhubIO_AddNumericPushHandler(path, HandlePeriodPush, sensorPtr);

    LE_ASSERT(snprintf(path, sizeof(path), "%s/trigger", name) < sizeof(path));
    result = dhubIO_CreateOutput(path, DHUBIO_DATA_TYPE_TRIGGER, "");
    if (result != LE_OK)
    {
        LE_FATAL("Failed to create Data Hub Output '%s' (%s).", path, LE_RESULT_TXT(result));
    }
    sensorPtr->triggerHandlerRef = dhubIO_AddTriggerPushHandler(path, HandleTriggerPush, sensorPtr);
    dhubIO_MarkOptional(path);

    return sensorPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Removes a periodic sensor scaffold and all associated resources
 */
//--------------------------------------------------------------------------------------------------
void psensor_Destroy
(
    psensor_Ref_t *ref
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr;
    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];


    LE_ASSERT(NULL != ref);

    sensorPtr = *ref;
    *ref = NULL;

    if (sensorPtr)
    {
        sensorPtr->isEnabled = false;

        // Stop and delete timer
        (void)le_timer_Stop (sensorPtr->timer);
        le_timer_Delete(sensorPtr->timer);

        // Deregister handlers and remove resources
        LE_ASSERT(snprintf(path, sizeof(path), "%s/trigger", sensorPtr->name) < sizeof(path));
        dhubIO_RemoveTriggerPushHandler(sensorPtr->triggerHandlerRef);
        dhubIO_DeleteResource(path);


        LE_ASSERT(snprintf(path, sizeof(path), "%s/period", sensorPtr->name) < sizeof(path));
        dhubIO_RemoveNumericPushHandler(sensorPtr->periodHandlerRef);
        dhubIO_DeleteResource(path);


        LE_ASSERT(snprintf(path, sizeof(path), "%s/enable", sensorPtr->name) < sizeof(path));
        dhubIO_RemoveBooleanPushHandler(sensorPtr->enableHandlerRef);
        dhubIO_DeleteResource(path);


        LE_ASSERT(snprintf(path, sizeof(path), "%s/value", sensorPtr->name) < sizeof(path));
        dhubIO_DeleteResource(path);

        dhubIO_DeleteResource(sensorPtr->name);

        free(sensorPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a boolean sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
void psensor_PushBoolean
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    bool value
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = ref;

    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];
    LE_ASSERT(snprintf(path, sizeof(path), "%s/value", sensorPtr->name) < sizeof(path));

    dhubIO_PushBoolean(path, timestamp, value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a numeric sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
void psensor_PushNumeric
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    double value
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = ref;

    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];
    LE_ASSERT(snprintf(path, sizeof(path), "%s/value", sensorPtr->name) < sizeof(path));

    dhubIO_PushNumeric(path, timestamp, value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a string sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
void psensor_PushString
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = ref;

    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];
    LE_ASSERT(snprintf(path, sizeof(path), "%s/value", sensorPtr->name) < sizeof(path));

    dhubIO_PushString(path, timestamp, value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a JSON sample to the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
void psensor_PushJson
(
    psensor_Ref_t ref,  ///< Reference returned by psensor_Create().
    double timestamp,   ///< Timestamp (or 0 = now).
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    Sensor_t* sensorPtr = ref;

    char path[DHUBIO_MAX_RESOURCE_PATH_LEN];
    LE_ASSERT(snprintf(path, sizeof(path), "%s/value", sensorPtr->name) < sizeof(path));

    dhubIO_PushJson(path, timestamp, value);
}


COMPONENT_INIT
{
}
