//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the Resource base class methods.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "dataHub.h"
#include "resource.h"
#include "ioPoint.h"
#include "obs.h"


/// Pool of Placeholder resource objects, which are instances of res_Resource_t.
static le_mem_PoolRef_t PlaceholderPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the Resource module.
 *
 * @warning Must be called before any other functions in this module.
 */
//--------------------------------------------------------------------------------------------------
void res_Init
(
    void
)
{
    PlaceholderPool = le_mem_CreatePool("Placeholder", sizeof(res_Resource_t));
    le_mem_SetDestructor(PlaceholderPool, (void (*)())res_Destruct);
}


//--------------------------------------------------------------------------------------------------
/**
 * Walk the routes leading from a given Resource to see if we can reach a given other Resource.
 *
 * @return true if pushing a data sample to the start resource would result in delivery to
 *         the destination resource.
 */
//--------------------------------------------------------------------------------------------------
static bool CanGetThereFromHere
(
    res_Resource_t* therePtr,   ///< See if we can reach this Resource.
    res_Resource_t* herePtr     ///< From this Resource.
)
//--------------------------------------------------------------------------------------------------
{
    // NOTE: This is a recursive function, but the recursion is bounded by the number of
    //       resources in the tree, because we take steps to guarantee that there are no
    //       loops in the routes.  Also, under typical usage scenarios, the number of route
    //       hops will not be longer than a few.

    le_dls_Link_t* linkPtr = le_dls_Peek(&(herePtr->destList));

    while (linkPtr != NULL)
    {
        res_Resource_t* destPtr = CONTAINER_OF(linkPtr, res_Resource_t, destListLink);

        if ((destPtr == therePtr) || CanGetThereFromHere(therePtr, destPtr))
        {
            // We found the Resource we were looking for.
            return true;
        }

        linkPtr = le_dls_PeekNext(&(herePtr->destList), linkPtr);
    }

    // We didn't find the Resource we were looking for.
    return false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Initializes Resource object.  (Constructor function for the res_Resource_t base class.)
 */
//--------------------------------------------------------------------------------------------------
static void InitResource
(
    res_Resource_t* resPtr,
    resTree_EntryRef_t entryRef ///< Reference to the resource tree entry this Resource is in.
)
//--------------------------------------------------------------------------------------------------
{
    resPtr->entryRef = entryRef;
    resPtr->units[0] = '\0';
    resPtr->currentValue = NULL;
    resPtr->currentType = IO_DATA_TYPE_TRIGGER;
    resPtr->pushedValue = NULL;
    resPtr->pushedType = IO_DATA_TYPE_TRIGGER;
    resPtr->srcPtr = NULL;
    resPtr->destList = LE_DLS_LIST_INIT;
    resPtr->destListLink = LE_DLS_LINK_INIT;
    resPtr->overrideValue = NULL;
    resPtr->defaultValue = NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the Units of a resource.
 */
//--------------------------------------------------------------------------------------------------
static void SetUnits
(
    res_Resource_t* resPtr,
    const char* units
)
//--------------------------------------------------------------------------------------------------
{
    if (le_utf8_Copy(resPtr->units, units, sizeof(resPtr->units), NULL) != LE_OK)
    {
        LE_CRIT("Units string too long!");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateInput
(
    resTree_EntryRef_t entryRef, ///< The entry that this resource will be attached to.
    io_DataType_t dataType,
    const char* units
)
//--------------------------------------------------------------------------------------------------
{
    res_Resource_t* resPtr = ioPoint_Create(dataType);

    InitResource(resPtr, entryRef);

    resPtr->currentType = dataType;
    SetUnits(resPtr, units);

    return resPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Output resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateOutput
(
    resTree_EntryRef_t entryRef, ///< The entry that this resource will be attached to.
    io_DataType_t dataType,
    const char* units
)
//--------------------------------------------------------------------------------------------------
{
    res_Resource_t* resPtr = ioPoint_Create(dataType);

    InitResource(resPtr, entryRef);

    resPtr->currentType = dataType;
    SetUnits(resPtr, units);

    return resPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Observation resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreateObservation
(
    resTree_EntryRef_t entryRef ///< The entry that this resource will be attached to.
)
//--------------------------------------------------------------------------------------------------
{
    res_Resource_t* resPtr = obs_Create();

    InitResource(resPtr, entryRef);

    return resPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create a Placeholder resource object.
 *
 * @return Ptr to the object.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* res_CreatePlaceholder
(
    resTree_EntryRef_t entryRef ///< The entry that this resource will be attached to.
)
//--------------------------------------------------------------------------------------------------
{
    res_Resource_t* resPtr = le_mem_ForceAlloc(PlaceholderPool);

    InitResource(resPtr, entryRef);

    return resPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Destruct a resource object.  This is called by the sub-class's destructor function to destruct
 * the parent class part of the object.
 */
//--------------------------------------------------------------------------------------------------
void res_Destruct
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    resPtr->entryRef = NULL;

    if (resPtr->currentValue != NULL)
    {
        le_mem_Release(resPtr->currentValue);
        resPtr->currentValue = NULL;
    }

    resPtr->pushedValue = NULL;
    if (resPtr->pushedValue != NULL)
    {
        LE_WARN("Resource had a pushed value.");
        le_mem_Release(resPtr->pushedValue);
        resPtr->pushedValue = NULL;
    }

    LE_ASSERT(resPtr->srcPtr == NULL);
    LE_ASSERT(le_dls_IsEmpty(&resPtr->destList));

    if (resPtr->pushedValue != NULL)
    {
        LE_WARN("Resource had an override value.");
        le_mem_Release(resPtr->overrideValue);
        resPtr->overrideValue = NULL;
    }
    resPtr->overrideValue = NULL;

    if (resPtr->defaultValue != NULL)
    {
        LE_WARN("Resource had a default value.");
        le_mem_Release(resPtr->defaultValue);
        resPtr->defaultValue = NULL;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the Units of a resource.
 *
 * @return Pointer to the units string (valid as long as the resource exists).
 */
//--------------------------------------------------------------------------------------------------
const char* res_GetUnits
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->units;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find out what data type a given resource currently has.
 *
 * Note that the data type of Inputs and Outputs are set by the app that creates those resources.
 * All other resources will change data types as values are pushed to them.
 *
 * @return the data type.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t res_GetDataType
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->currentType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the current value of a resource (last accepted pushed value or default value).
 *
 * @return Reference to the Data Sample object or NULL if the resource doesn't have a current value.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetCurrentValue
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->currentValue;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the source resource of a given resource.
 *
 * Does nothing if the route already exists.
 *
 * @warning Both source and destination MUST be resources, not Namespace resource tree entries.
 *
 * @return
 *  - LE_OK if route already existed or new route was successfully created.
 *  - LE_DUPLICATE if the addition of this route would result in a loop.
 */
//--------------------------------------------------------------------------------------------------
le_result_t res_SetSource
(
    res_Resource_t* destPtr, ///< [IN] The destination resource.
    res_Resource_t* srcPtr   ///< [IN] The source resource (NULL to clear).
)
//--------------------------------------------------------------------------------------------------
{
    // If the source is already set the way we want it, just return OK.
    if (destPtr->srcPtr == srcPtr)
    {
        return LE_OK;
    }

    // If the destination has some other source, remove that.
    if (destPtr->srcPtr != NULL)
    {
        le_dls_Remove(&(destPtr->srcPtr->destList), &(destPtr->destListLink));
        destPtr->srcPtr = NULL;
    }

    // If we are setting a non-NULL source,
    if (srcPtr != NULL)
    {
        // Check if we can get back to the source resource from the destination resource by
        // following pre-existing routes.  If we can, then this new route would create a loop.
        if (CanGetThereFromHere(srcPtr /* there */, destPtr /* here */))
        {
            return LE_DUPLICATE;
        }

        // Connect the source.
        le_dls_Queue(&(srcPtr->destList), &(destPtr->destListLink));
        destPtr->srcPtr = srcPtr;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Fetches the data flow source resource entry from which a given resource expects to receive data
 * samples.
 *
 * @return Reference to the source entry or NULL if none configured.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t res_GetSource
(
    res_Resource_t* destPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (destPtr->srcPtr != NULL)
    {
        return destPtr->srcPtr->entryRef;
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Update the current value of a resource.  This can have the side effect of pushing the value
 * out to other resources or apps that have registered to receive Pushes from this resource.
 *
 * @note This function takes ownership of the dataSample reference it is passed.
 */
//--------------------------------------------------------------------------------------------------
static void UpdateCurrentValue
(
    res_Resource_t* resPtr,         ///< The resource to push to.
    io_DataType_t dataType,         ///< The data type.
    dataSample_Ref_t dataSample     ///< The data sample (timestamp + value).
)
//--------------------------------------------------------------------------------------------------
{
    // Set the current value to the new data sample.
    if (resPtr->currentValue != NULL)
    {
        le_mem_Release(resPtr->currentValue);
    }
    resPtr->currentType = dataType;
    resPtr->currentValue = dataSample;

    // Iterate over the list of destination routes, pushing to all of them.
    le_dls_Link_t* linkPtr = le_dls_Peek(&(resPtr->destList));
    while (linkPtr != NULL)
    {
        res_Resource_t* destPtr = CONTAINER_OF(linkPtr, res_Resource_t, destListLink);

        // Increment the reference count before pushing.
        le_mem_AddRef(dataSample);

        res_Push(destPtr, dataType, resPtr->units, dataSample);

        linkPtr = le_dls_PeekNext(&(resPtr->destList), linkPtr);
    }

    // Take futher action by calling the sub-class's processing function.
    switch (resTree_GetEntryType(resPtr->entryRef))
    {
        case ADMIN_ENTRY_TYPE_INPUT:
        case ADMIN_ENTRY_TYPE_OUTPUT:

            ioPoint_ProcessAccepted(resPtr, dataType, dataSample);
            break;

        case ADMIN_ENTRY_TYPE_OBSERVATION:

            obs_ProcessAccepted(resPtr, dataType, dataSample);
            break;

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:

            // Placeholders don't do any additional processing.
            break;

        default:
            LE_FATAL("Unexpected entry type.");
            break;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Push a data sample to a resource.
 *
 * @note Takes ownership of the data sample reference.
 */
//--------------------------------------------------------------------------------------------------
void res_Push
(
    res_Resource_t* resPtr,         ///< The resource to push to.
    io_DataType_t dataType,         ///< The data type.
    const char* units,              ///< The units (NULL = take on resource's units)
    dataSample_Ref_t dataSample     ///< The data sample (timestamp + value).
)
//--------------------------------------------------------------------------------------------------
{
    LE_ASSERT(resPtr->entryRef != NULL);
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    // Record this as the latest pushed value, even if it doesn't get accepted as the new
    // current value.
    if (resPtr->pushedValue != NULL)
    {
        le_mem_Release(resPtr->pushedValue);
    }
    resPtr->pushedValue = dataSample;
    resPtr->pushedType = dataType;

    // Ask the sub-class if this should be accepted as the new current value.
    bool accepted;
    switch (entryType)
    {
        case ADMIN_ENTRY_TYPE_INPUT:
        case ADMIN_ENTRY_TYPE_OUTPUT:

            accepted = ioPoint_ShouldAccept(resPtr, dataType, units);
            break;

        case ADMIN_ENTRY_TYPE_OBSERVATION:

            accepted = obs_ShouldAccept(resPtr, dataType, dataSample);
            break;

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:

            accepted = true;  // Placeholders accept everything.
            break;

        default:
            LE_FATAL("Unexpected entry type.");
            break;
    }

    if (accepted)
    {
        // If an override is in effect, the current value becomes a new data sample that has
        // the same timestamp as the pushed sample but the override's value.
        if (resPtr->overrideValue != NULL)
        {
            dataSample_Ref_t overrideSample = dataSample_Copy(resPtr->overrideType,
                                                              resPtr->overrideValue);
            dataSample_SetTimestamp(overrideSample, dataSample_GetTimestamp(dataSample));
            dataType = resPtr->overrideType;
            dataSample = overrideSample;
        }
        else
        {
            // If we're passing on the pushed sample as the next current value, then
            // increment the reference count.
            le_mem_AddRef(dataSample);
        }

        // If the units were provided, and this is an Observation or a Placeholder
        // (which are "units-flexible"), then apply the units to this resource.
        if ((units != NULL) && (   (entryType == ADMIN_ENTRY_TYPE_OBSERVATION)
                                || (entryType == ADMIN_ENTRY_TYPE_PLACEHOLDER)  )  )
        {
            SetUnits(resPtr, units);
        }

        UpdateCurrentValue(resPtr, dataType, dataSample);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Add a Push Handler to an Output resource.
 *
 * @return Reference to the handler added.
 */
//--------------------------------------------------------------------------------------------------
hub_HandlerRef_t res_AddPushHandler
(
    res_Resource_t* resPtr, ///< Ptr to the Output resource.
    io_DataType_t dataType,
    void* callbackPtr,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    return ioPoint_AddPushHandler(resPtr, dataType, callbackPtr, contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove a Push Handler from an Output resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemovePushHandler
(
    hub_HandlerRef_t handlerRef
)
//--------------------------------------------------------------------------------------------------
{
    ioPoint_RemovePushHandler(handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Check whether a given resource has administrative settings.
 *
 * @note Currently ignores Observation-specific settings, because this function isn't needed
 *       for Observations.
 *
 * @return true if there are some administrative settings present, false otherwise.
 */
//--------------------------------------------------------------------------------------------------
bool res_HasAdminSettings
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    // Currently ignores Observation-specific settings, because this function isn't needed
    // for Observations.
    LE_ASSERT(resTree_GetEntryType(resPtr->entryRef) != ADMIN_ENTRY_TYPE_OBSERVATION);

    return (   (resPtr->srcPtr != NULL)     // Source
            || (!le_dls_IsEmpty(&resPtr->destList)) // Destination list
            || (resPtr->overrideValue != NULL) // Override
            || (resPtr->defaultValue != NULL) ); // Default
}


//--------------------------------------------------------------------------------------------------
/**
 * Move the administrative settings from one Resource object to another of a different type.
 *
 * @note Inputs and Outputs don't have any special admin settings over and above the settings
 *       that all other types of resources have.  Furthermore, because we are never moving the
 *       settings between two resources of the same type, we don't have to move any
 *       Observation-specific settings.
 */
//--------------------------------------------------------------------------------------------------
void res_MoveAdminSettings
(
    res_Resource_t* srcPtr,   ///< Move settings from this entry
    res_Resource_t* destPtr   ///< Move settings to this entry
)
//--------------------------------------------------------------------------------------------------
{
    LE_ASSERT(srcPtr->entryRef != NULL);
    LE_ASSERT(destPtr->entryRef != NULL);

    // Copy over the units string.
    LE_ASSERT(LE_OK == le_utf8_Copy(destPtr->units, srcPtr->units, sizeof(destPtr->units), NULL));

    // Move the current value
    destPtr->currentType = srcPtr->currentType;
    destPtr->currentValue = srcPtr->currentValue;
    srcPtr->currentValue = NULL; // dest took the reference count

    // Move the last pushed value
    destPtr->pushedType = srcPtr->pushedType;
    destPtr->pushedValue = srcPtr->pushedValue;
    srcPtr->pushedValue = NULL; // dest took the reference count

    // Move the data source
    destPtr->srcPtr = srcPtr->srcPtr;
    srcPtr->srcPtr = NULL;
    if (destPtr->srcPtr != NULL)
    {
        le_dls_Remove(&destPtr->srcPtr->destList, &srcPtr->destListLink);
        le_dls_Queue(&destPtr->srcPtr->destList, &destPtr->destListLink);
    }

    // Move the list of destinations
    le_dls_Link_t* linkPtr;
    while ((linkPtr = le_dls_Pop(&srcPtr->destList)) != NULL)
    {
        res_Resource_t* routeDestPtr = CONTAINER_OF(linkPtr, res_Resource_t, destListLink);
        le_dls_Queue(&destPtr->destList, linkPtr);
        routeDestPtr->srcPtr = destPtr;
    }

    // Move the override
    destPtr->overrideType = srcPtr->overrideType;
    destPtr->overrideValue = srcPtr->overrideValue;
    srcPtr->overrideValue = NULL;

    // Move the default value
    destPtr->defaultType = srcPtr->defaultType;
    destPtr->defaultValue = srcPtr->defaultValue;
    srcPtr->defaultValue = NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Drop all the resource settings.
 */
//--------------------------------------------------------------------------------------------------
static void DropSettings
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    // Drop the current value
    if (resPtr->currentValue != NULL)
    {
        le_mem_Release(resPtr->currentValue);
        resPtr->currentValue = NULL;
    }

    // Drop the last pushed value
    if (resPtr->pushedValue != NULL)
    {
        le_mem_Release(resPtr->pushedValue);
        resPtr->pushedValue = NULL;
    }

    // Remove the data source
    res_SetSource(resPtr, NULL);

    // Remove all destinations
    le_dls_Link_t* linkPtr;
    while ((linkPtr = le_dls_Peek(&resPtr->destList)) != NULL)
    {
        res_Resource_t* destPtr = CONTAINER_OF(linkPtr, res_Resource_t, destListLink);

        res_SetSource(destPtr, NULL);
    }

    // Drop the override
    if (resPtr->overrideValue != NULL)
    {
        le_mem_Release(resPtr->overrideValue);
        resPtr->overrideValue = NULL;
    }

    // Drop the default value
    if (resPtr->defaultValue != NULL)
    {
        le_mem_Release(resPtr->defaultValue);
        resPtr->defaultValue = NULL;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Delete an Observation.
 */
//--------------------------------------------------------------------------------------------------
void res_DeleteObservation
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    DropSettings(resPtr);

    le_mem_Release(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum period between data samples accepted by a given Observation.
 *
 * This is used to throttle the rate of data passing into and through an Observation.
 */
//--------------------------------------------------------------------------------------------------
void res_SetMinPeriod
(
    res_Resource_t* resPtr,
    double minPeriod
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetMinPeriod(resPtr, minPeriod);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum period between data samples accepted by a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetMinPeriod
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetMinPeriod(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the highest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void res_SetHighLimit
(
    res_Resource_t* resPtr,
    double highLimit
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetHighLimit(resPtr, highLimit);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the highest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetHighLimit
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetHighLimit(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the lowest value in a range that will be accepted by a given Observation.
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void res_SetLowLimit
(
    res_Resource_t* resPtr,
    double lowLimit
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetLowLimit(resPtr, lowLimit);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the lowest value in a range that will be accepted by a given Observation.
 *
 * @return The value, or NAN (not a number) if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetLowLimit
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetLowLimit(resPtr);
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
void res_SetChangeBy
(
    res_Resource_t* resPtr,
    double change
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetChangeBy(resPtr, change);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the magnitude that a new value must vary from the current value to be accepted by
 * a given Observation.
 *
 * @return The value, or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
double res_GetChangeBy
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetChangeBy(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the maximum number of data samples to buffer in a given Observation.  Buffers are FIFO
 * circular buffers. When full, the buffer drops the oldest value to make room for a new addition.
 */
//--------------------------------------------------------------------------------------------------
void res_SetBufferMaxCount
(
    res_Resource_t* resPtr,
    uint32_t count
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetBufferMaxCount(resPtr, count);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the buffer size setting for a given Observation.
 *
 * @return The buffer size (in number of samples) or 0 if not set.
 */
//--------------------------------------------------------------------------------------------------
uint32_t res_GetBufferMaxCount
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetBufferMaxCount(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the minimum time between backups of an Observation's buffer to non-volatile storage.
 * If the buffer's size is non-zero and the backup period is non-zero, then the buffer will be
 * backed-up to non-volatile storage when it changes, but never more often than this period setting
 * specifies.
 */
//--------------------------------------------------------------------------------------------------
void res_SetBufferBackupPeriod
(
    res_Resource_t* resPtr,
    uint32_t seconds
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetBufferBackupPeriod(resPtr, seconds);
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
uint32_t res_GetBufferBackupPeriod
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetBufferBackupPeriod(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the default value of a resource.
 *
 * Will be discarded if setting the default value on an Input or Output that doesn't have the
 * same data type.
 */
//--------------------------------------------------------------------------------------------------
void res_SetDefault
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    dataSample_Ref_t value  /// Ownership of this reference is passed to the resource.
)
//--------------------------------------------------------------------------------------------------
{
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    if (   ((entryType == ADMIN_ENTRY_TYPE_INPUT) || (entryType == ADMIN_ENTRY_TYPE_OUTPUT))
        && (dataType != res_GetDataType(resPtr))   )
    {
        LE_WARN("Discarding default: type mismatch.");
        le_mem_Release(value);
    }
    else
    {
        if (resPtr->defaultValue != NULL)
        {
            le_mem_Release(resPtr->defaultValue);
        }

        resPtr->defaultValue = value;
        resPtr->defaultType = dataType;

        // If this resource is currently operating on its default value
        // (doesn't have an override or anything pushed to it), update
        // the current value to this value.
        if (   (resPtr->overrideValue == NULL)
            && ((resPtr->pushedValue == NULL) || (resPtr->srcPtr == NULL))  )
        {
            le_mem_AddRef(value);
            UpdateCurrentValue(resPtr, dataType, value);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Discover whether a given resource has a default value.
 *
 * @return true if there is a default value set, false if not.
 */
//--------------------------------------------------------------------------------------------------
bool res_HasDefault
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return (resPtr->defaultValue != NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the data type of the default value that is currently set on a given resource.
 *
 * @return The data type, or IO_DATA_TYPE_TRIGGER if not set.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t res_GetDefaultDataType
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (resPtr->defaultValue == NULL)
    {
        return IO_DATA_TYPE_TRIGGER;
    }
    return resPtr->defaultType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the default value of a resource.
 *
 * @return the default value or NULL if not set.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetDefaultValue
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->defaultValue;
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove any default value that might be set on a given resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemoveDefault
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (resPtr->defaultValue != NULL)
    {
        le_mem_Release(resPtr->defaultValue);
        resPtr->defaultValue = NULL;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set an override on a given resource.
 *
 * @note Override will be discarded by an Input or Output resource if the override's data type
 *       does not match the data type of the Input or Output.
 */
//--------------------------------------------------------------------------------------------------
void res_SetOverride
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    dataSample_Ref_t value
)
//--------------------------------------------------------------------------------------------------
{
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    // Inputs and Outputs have fixed types specified by the apps that create them.
    // Reject any override that is of a different type if this is an Input or Output.
    if (   ((entryType == ADMIN_ENTRY_TYPE_INPUT) || (entryType == ADMIN_ENTRY_TYPE_OUTPUT))
        && (dataType != ioPoint_GetDataType(resPtr))  )
    {
        LE_WARN("Ignoring override: data type mismatch.");
    }
    else
    {
        if (resPtr->overrideValue != NULL)
        {
            le_mem_Release(resPtr->overrideValue);
        }
        resPtr->overrideValue = value;
        resPtr->overrideType = dataType;

        // Update the current value to this value now.
        le_mem_AddRef(value);
        UpdateCurrentValue(resPtr, dataType, value);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Find out whether the resource currently has an override in effect.
 *
 * @return true if the resource is overridden, false otherwise.
 */
//--------------------------------------------------------------------------------------------------
bool res_IsOverridden
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return (resPtr->overrideValue != NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove any override that might be in effect for a given resource.
 */
//--------------------------------------------------------------------------------------------------
void res_RemoveOverride
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (resPtr->overrideValue != NULL)
    {
        le_mem_Release(resPtr->overrideValue);
        resPtr->overrideValue = NULL;

        // If the resource has a pushed value, update the current value to that.
        if (resPtr->pushedValue != NULL)
        {
            le_mem_AddRef(resPtr->pushedValue);
            UpdateCurrentValue(resPtr, resPtr->pushedType, resPtr->pushedValue);
        }
        // Otherwise, look for a default value,
        else if (resPtr->defaultValue != NULL)
        {
            le_mem_AddRef(resPtr->defaultValue);
            UpdateCurrentValue(resPtr, resPtr->defaultType, resPtr->defaultValue);
        }
    }
}
