//--------------------------------------------------------------------------------------------------
/**
 * @file obs.c
 *
 * Implementation of Observations.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "dataHub.h"
#include "nan.h"
#include "dataSample.h"
#include "resource.h"
#include "resTree.h"


/// Observation Resource.  Allocated from the Observation Pool.
typedef struct
{
    res_Resource_t resource;    ///< The base class (MUST BE FIRST).

    double highLimit; ///< Filter deadband/liveband high limit; NAN = disabled.
    double lowLimit;  ///< Filter deadband/liveband low limit; NAN = disabled.
    double changeBy;  ///< Drop values that differ by less than this from current; NAN/0 = disabled.

    double minPeriod; ///< Min number of seconds before accepting another value; NAN/0 = disabled.
    uint32_t lastPushTime; ///< Time at which last push was accepted (ms, relative clock).

    size_t maxCount;  ///< Maximum number of entries to buffer.
    size_t count;     ///< Current number of entries in the buffer.

    uint32_t backupPeriod; ///< Min time (in seconds) between non-volatile backups of the buffer.
    uint32_t lastBackupTime; ///< Time at which last push was accepted (ms, relative clock).

    le_sls_List_t sampleList; ///< List of buffered data samples.
}
Observation_t;


/// Object used to link a Data Sample into an Observation's buffer.
/// Holds a reference on the Data Sample object.
typedef struct
{
    le_sls_Link_t dataSetLink;  ///< Used to link into a Data Set's sampleList.
    dataSample_Ref_t sampleRef; ///< Reference to the Data Sample object.
}
BufferEntry_t;


/// Pool of Observation objects.
static le_mem_PoolRef_t ObservationPool = NULL;

/// Pool of Buffer Entry objects.
static le_mem_PoolRef_t BufferEntryPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Get the relative time in milliseconds.
 *
 * @return the relative time (ms).
 */
//--------------------------------------------------------------------------------------------------
static uint32_t GetRelativeTimeMs
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    le_clk_Time_t structuredTime = le_clk_GetRelativeTime();

    return (structuredTime.sec * 1000 + structuredTime.usec / 1000);
}


//--------------------------------------------------------------------------------------------------
/**
 * Observation destructor.
 */
//--------------------------------------------------------------------------------------------------
static void ObservationDestructor
(
    void* objectPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = objectPtr;

    LE_INFO("Destructing observation '%s'.", resTree_GetEntryName(obsPtr->resource.entryRef));

    // Delete all the buffered data samples.


    // TODO: Delete the buffer backup file if backups are enabled.

    res_Destruct(&obsPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Buffer Entry destructor.
 */
//--------------------------------------------------------------------------------------------------
static void BufferEntryDestructor
(
    void* objectPtr
)
//--------------------------------------------------------------------------------------------------
{
    BufferEntry_t* buffEntryPtr = objectPtr;

    le_mem_Release(buffEntryPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Observation module.
 *
 * @warning This must be called before any other function in this module is called.
 */
//--------------------------------------------------------------------------------------------------
void obs_Init
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    ObservationPool = le_mem_CreatePool("Observation", sizeof(Observation_t));
    le_mem_SetDestructor(ObservationPool, ObservationDestructor);

    BufferEntryPool = le_mem_CreatePool("Buffer Entry", sizeof(BufferEntry_t));
    le_mem_SetDestructor(BufferEntryPool, BufferEntryDestructor);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Observation object.  This allocates the object and initializes the class members,
 * but not the parent class members.
 *
 * @return Pointer to the new object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* obs_Create
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = le_mem_ForceAlloc(ObservationPool);

    obsPtr->lowLimit = NAN;
    obsPtr->highLimit = NAN;
    obsPtr->changeBy = NAN;
    obsPtr->minPeriod = NAN;

    obsPtr->maxCount = 0;
    obsPtr->count = 0;

    obsPtr->sampleList = LE_SLS_LIST_INIT;

    return &obsPtr->resource;
}


//--------------------------------------------------------------------------------------------------
/**
 * Determine whether a given value should be accepted by an Observation.
 */
//--------------------------------------------------------------------------------------------------
bool obs_ShouldAccept
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    dataSample_Ref_t value
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    // Check the high limit and low limit first,.
    if (dataType == IO_DATA_TYPE_NUMERIC)
    {
        double numericValue = dataSample_GetNumeric(value);

        // If both limits are enabled and the low limit is higher than the high limit, then
        // this is the "deadband" case. ( - <------HxxxxxxxxxL------> + )
        if (   (obsPtr->highLimit != NAN)
            && (obsPtr->lowLimit != NAN)
            && (obsPtr->lowLimit > obsPtr->highLimit)  )
        {
            if ((numericValue > obsPtr->lowLimit) || (numericValue < obsPtr->highLimit))
            {
                return false;
            }
        }
        // In all other cases, reject if lower than non-NAN low limit or higher than non-NAN high.
        else
        {
            if ((obsPtr->lowLimit != NAN) && (numericValue < obsPtr->lowLimit))
            {
                return false;
            }

            if ((obsPtr->highLimit != NAN) && (numericValue > obsPtr->highLimit))
            {
                return false;
            }
        }
    }

    // The current time in ms since some unspecified start time.
    uint32_t now = 0;

    // If we have received a push before (giving us something to compare against),
    // Check the minPeriod and changeBy,
    dataSample_Ref_t previousValue = res_GetCurrentValue(resPtr);
    if (previousValue != NULL)
    {
        // If there is a changedBy filter in effect,
        if ((obsPtr->changeBy != 0) && (obsPtr->changeBy != NAN))
        {
            // If overridden, reject everything because the value won't change.
            if (res_IsOverridden(resPtr))
            {
                return false;
            }

            // If the data type has changed, we can't do a comparison, so only check the changeBy
            // filter if the types are the same.
            if (dataType == res_GetDataType(resPtr))
            {
                // If this is a numeric value and the last value was numeric too,
                if (dataType == IO_DATA_TYPE_NUMERIC)
                {
                    // Reject changes in the current value smaller than the changeBy setting.
                    double previousNumber = dataSample_GetNumeric(previousValue);
                    if (fabs(dataSample_GetNumeric(value) - previousNumber) < obsPtr->changeBy)
                    {
                        return false;
                    }
                }
                // For Boolean, a non-zero changeBy means filter out if unchanged.
                else if (dataType == IO_DATA_TYPE_BOOLEAN)
                {
                    if (dataSample_GetBoolean(value) == dataSample_GetBoolean(previousValue))
                    {
                        return false;
                    }
                }
                // For string or JSON, a non-zero changeBy means filter out if unchanged.
                else if ((dataType == IO_DATA_TYPE_STRING) || (dataType == IO_DATA_TYPE_JSON))
                {
                    if (0 == strcmp(dataSample_GetString(value),
                                    dataSample_GetString(previousValue)))
                    {
                        return false;
                    }
                }
            }
        }

        // All of the above can be done without a system call, so that's why we do the
        // minPeriod check last.
        if ((obsPtr->minPeriod != 0) && (obsPtr->minPeriod != NAN))
        {
            now = GetRelativeTimeMs();  // system call

            if ((now - obsPtr->lastPushTime) < (obsPtr->minPeriod * 1000))
            {
                return false;
            }
        }
    }

    // Update the time of last update.
    if (now == 0)
    {
        now = GetRelativeTimeMs(); // system call
    }
    obsPtr->lastPushTime = now;

    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform processing of an accepted pushed data sample that is specific to an Observation
 * resource.
 */
//--------------------------------------------------------------------------------------------------
void obs_ProcessAccepted
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< Data type of the data sample.
    dataSample_Ref_t sampleRef  ///< Data sample.
)
//--------------------------------------------------------------------------------------------------
{
    // TODO: Implement buffering.
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum period between data samples accepted by a given Observation.
 *
 * This is used to throttle the rate of data passing into and through an Observation.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetMinPeriod
(
    res_Resource_t* resPtr,
    double minPeriod
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->minPeriod = minPeriod;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum period between data samples accepted by a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double obs_GetMinPeriod
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->minPeriod;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the highest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetHighLimit
(
    res_Resource_t* resPtr,
    double highLimit
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->highLimit = highLimit;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the highest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double obs_GetHighLimit
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->highLimit;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the lowest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetLowLimit
(
    res_Resource_t* resPtr,
    double lowLimit
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->lowLimit = lowLimit;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the lowest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double obs_GetLowLimit
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->lowLimit;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the magnitude that a new value must vary from the current value to be accepted by
 * a given Observation.
 *
 * Ignored for trigger types.
 *
 * For all other types, any non-zero value means accept any change, but drop if the same as current.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetChangeBy
(
    res_Resource_t* resPtr,
    double change
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->changeBy = change;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the magnitude that a new value must vary from the current value to be accepted by
 * a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double obs_GetChangeBy
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->changeBy;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the maximum number of data samples to buffer in a given Observation.  Buffers are FIFO
 * circular buffers. When full, the buffer drops the oldest value to make room for a new addition.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetBufferMaxCount
(
    res_Resource_t* resPtr,
    uint32_t count
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->maxCount = count;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the buffer size setting for a given Observation.
 *
 * @return The buffer size (in number of samples) or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
uint32_t obs_GetBufferMaxCount
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->maxCount;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum time between backups of an Observation's buffer to non-volatile storage.
 * If the buffer's size is non-zero and the backup period is non-zero, then the buffer will be
 * backed-up to non-volatile storage when it changes, but never more often than this period setting
 * specifies.
 */
//--------------------------------------------------------------------------------------------------
void obs_SetBufferBackupPeriod
(
    res_Resource_t* resPtr,
    uint32_t seconds
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    obsPtr->backupPeriod = seconds;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum time between backups of an Observation's buffer to non-volatile storage.
 * See admin_SetBufferBackupPeriod() for more information.
 *
 * @return The buffer backup period (in seconds) or 0 if backups are disabled or the Observation
 *         does not exist.
 */
//--------------------------------------------------------------------------------------------------
uint32_t obs_GetBufferBackupPeriod
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    return obsPtr->backupPeriod;
}
