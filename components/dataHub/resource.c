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
#include "handler.h"


/// true if an extended configuration update is in progress, false if in normal operating mode.
static bool IsUpdateInProgress = false;


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
 * Figure out whether values of a given data type are acceptable for a given resource.
 *
 * @note If not, a type conversion will have to be done before the value can be used as the
 *       resource's current value.
 *
 * @return true if acceptable.  false if should be ignored.
 */
//--------------------------------------------------------------------------------------------------
static bool IsAcceptable
(
    res_Resource_t* resPtr,
    io_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    // Inputs and Outputs are fixed-type, so the data type must match for those types of resource.
    // Other types of resources (Observations and Placeholders) will accept any type of data.
    return (   ((entryType != ADMIN_ENTRY_TYPE_INPUT) && (entryType != ADMIN_ENTRY_TYPE_OUTPUT))
            || (dataType == ioPoint_GetDataType(resPtr))  );
}


//--------------------------------------------------------------------------------------------------
/**
 * Constructor for the Resource base class.
 *
 * @warning This is only for use by sub-classes (obs.c and ioPoint.c).
 */
//--------------------------------------------------------------------------------------------------
void res_Construct
(
    res_Resource_t* resPtr,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
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
    resPtr->overrideType = IO_DATA_TYPE_TRIGGER;
    resPtr->defaultValue = NULL;
    resPtr->defaultType = IO_DATA_TYPE_TRIGGER;
    resPtr->isConfigChanging = false;
    resPtr->pushHandlerList = LE_DLS_LIST_INIT;
    resPtr->jsonExample = NULL;
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
    res_Resource_t* resPtr = ioPoint_CreateInput(dataType, entryRef);

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
    res_Resource_t* resPtr = ioPoint_CreateOutput(dataType, entryRef);

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
    return obs_Create(entryRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Restore an Observation's data buffer from non-volatile backup, if one exists.
 */
//--------------------------------------------------------------------------------------------------
void res_RestoreBackup
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    LE_ASSERT(resTree_GetEntryType(resPtr->entryRef) == ADMIN_ENTRY_TYPE_OBSERVATION);

    obs_RestoreBackup(resPtr);
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

    res_Construct(resPtr, entryRef);

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

    if (resPtr->pushedValue != NULL)
    {
        LE_WARN("Resource had a pushed value.");
        le_mem_Release(resPtr->pushedValue);
        resPtr->pushedValue = NULL;
    }

    LE_ASSERT(resPtr->srcPtr == NULL);
    LE_ASSERT(le_dls_IsEmpty(&resPtr->destList));

    if (resPtr->overrideValue != NULL)
    {
        LE_WARN("Resource had an override value.");
        le_mem_Release(resPtr->overrideValue);
        resPtr->overrideValue = NULL;
    }

    if (resPtr->defaultValue != NULL)
    {
        LE_WARN("Resource had a default value.");
        le_mem_Release(resPtr->defaultValue);
        resPtr->defaultValue = NULL;
    }

    handler_RemoveAll(&resPtr->pushHandlerList);

    if (resPtr->jsonExample != NULL)
    {
        LE_WARN("Resource had a JSON example value.");
        le_mem_Release(resPtr->jsonExample);
        resPtr->jsonExample = NULL;
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

        // Propagate the source's JSON example value, if it has one and this resource accepts JSON.
        if ((srcPtr->jsonExample != NULL) && IsAcceptable(destPtr, IO_DATA_TYPE_JSON))
        {
            le_mem_AddRef(srcPtr->jsonExample);
            res_SetJsonExample(destPtr, srcPtr->jsonExample);
        }

        // If an extended update is in progress, flag that the configuration of both the
        // source and destination resources are changing, so acceptance of new pushed values
        // should be suspended until the update finishes.
        if (IsUpdateInProgress)
        {
            srcPtr->isConfigChanging = true;
            destPtr->isConfigChanging = true;
        }
    }
    // If the source is being set to a NULL source (removing the source) and the resource is
    // units-flexible (Observation or Placeholder), then clear the units string.
    else
    {
        admin_EntryType_t entryType = resTree_GetEntryType(destPtr->entryRef);

        if (   (entryType == ADMIN_ENTRY_TYPE_OBSERVATION)
            || (entryType == ADMIN_ENTRY_TYPE_PLACEHOLDER)  )
        {
            SetUnits(destPtr, "");
        }
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
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    // Check for type mismatches.
    if (!IsAcceptable(resPtr, dataType))
    {
        LE_WARN("Type mismatch: Ignoring '%s' for '%s' resource of type '%s'.",
                hub_GetDataTypeName(dataType),
                hub_GetEntryTypeName(entryType),
                hub_GetDataTypeName(ioPoint_GetDataType(resPtr)));

        le_mem_Release(dataSample);

        return;
    }

    // Set the current value to the new data sample.
    if (resPtr->currentValue != NULL)
    {
        le_mem_Release(resPtr->currentValue);
    }
    resPtr->currentType = dataType;
    resPtr->currentValue = dataSample;

    // If data type is JSON and there isn't a JSON example value for this resource yet,
    // then make this the JSON example value.
    if (dataType == IO_DATA_TYPE_JSON)
    {
        if (resPtr->jsonExample == NULL)
        {
            le_mem_AddRef(dataSample);
            resPtr->jsonExample = dataSample;
        }
    }
    // If the data type is not JSON, drop any existing JSON example value.
    else if (resPtr->jsonExample != NULL)
    {
        le_mem_Release(resPtr->jsonExample);
        resPtr->jsonExample = NULL;
    }

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

    // Call any the push handlers that match the data type of the sample.
    handler_CallAll(&resPtr->pushHandlerList, dataType, dataSample);
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
    const char* units,              ///< The units (NULL or "" = take on resource's units)
    dataSample_Ref_t dataSample     ///< The data sample (timestamp + value).
)
//--------------------------------------------------------------------------------------------------
{
    LE_ASSERT(resPtr->entryRef != NULL);

    if ((units != NULL) && (*units == '\0'))
    {
        units = NULL;
    }

    if (ADMIN_ENTRY_TYPE_OBSERVATION == resTree_GetEntryType(resPtr->entryRef))
    {
        // Do JSON extraction (if applicable) before filtering.
        if (obs_DoJsonExtraction(resPtr, &dataType, &dataSample) != LE_OK)
        {
            le_mem_Release(dataSample);
            return;
        }

        // Buffer and possibly backup the sample
        obs_ProcessAccepted(resPtr, dataType, dataSample);

        // Perform any transforms on the buffered data
        dataSample = obs_ApplyTransform(resPtr, dataType, dataSample);

        if (true != obs_ShouldAccept(resPtr, dataType, dataSample))
        {
            le_mem_Release(dataSample);
            return;
        }
    }

    // Record this as the latest pushed value, even if it doesn't get accepted as the new
    // current value.
    if (resPtr->pushedValue != NULL)
    {
        le_mem_Release(resPtr->pushedValue);
    }
    le_mem_AddRef(dataSample);
    resPtr->pushedValue = dataSample;
    resPtr->pushedType = dataType;

    // If the resource is undergoing a change to its routing or filtering configuration,
    // then acceptance of new samples is suspended until the configuration change is done.
    if (resPtr->isConfigChanging)
    {
        LE_WARN("Rejecting pushed value because configuration update is in progress.");
        le_mem_Release(dataSample);
        return;
    }

    // If an override is in effect, the current value becomes a new data sample that has
    // the same timestamp as the pushed sample but the override's value (and we drop the
    // original sample).
    if (res_IsOverridden(resPtr))
    {
        dataSample_Ref_t overrideSample = dataSample_Copy(resPtr->overrideType,
                                                          resPtr->overrideValue);
        dataSample_SetTimestamp(overrideSample, dataSample_GetTimestamp(dataSample));
        dataType = resPtr->overrideType;
        le_mem_Release(dataSample);
        dataSample = overrideSample;
        units = NULL;   // Get units from resource.
    }

    switch (resTree_GetEntryType(resPtr->entryRef))
    {
        case ADMIN_ENTRY_TYPE_INPUT:
        case ADMIN_ENTRY_TYPE_OUTPUT:

            // Check for units mismatches.
            // But, ignore the units if the units are supposed to be obtained from the resource,
            // or if the receiving resource doesn't have units.
            if (   (units != NULL)
                && (resPtr->units[0] != '\0')
                && (strcmp(units, resPtr->units) != 0)  )
            {
                LE_WARN("Rejecting push: units mismatch (pushing '%s' to '%s').",
                        units,
                        resPtr->units);
                le_mem_Release(dataSample);
                return;
            }

            // Inputs and outputs have a fixed type.  This means that if a different type
            // of value is received, we must do a type conversion before we can accept it.
            ioPoint_DoTypeCoercion(resPtr, &dataType, &dataSample);
            break;

        case ADMIN_ENTRY_TYPE_OBSERVATION:

            // *** FALL THROUGH ***

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:

            // Note: Placeholders accept everything.

            // This is a units-flexible resource, so if the units were provided,
            // apply the units to this resource.
            if (units != NULL)
            {
                SetUnits(resPtr, units);
            }

            break;

        default:
            LE_FATAL("Unexpected entry type.");
            break;
    }

    UpdateCurrentValue(resPtr, dataType, dataSample);
}


//--------------------------------------------------------------------------------------------------
/**
 * Add a Push Handler to an Output resource.
 *
 * @return Reference to the handler added.
 *
 * @note Can be removed by calling handler_Remove().
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
    LE_ASSERT(resTree_IsResource(resPtr->entryRef));

    return handler_Add(&resPtr->pushHandlerList, dataType, callbackPtr, contextPtr);
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
            || (resPtr->defaultValue != NULL) // Default
            || (! le_dls_IsEmpty(&resPtr->pushHandlerList)) ); // Push handlers
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
    res_Resource_t* destPtr,  ///< Move settings to this entry
    admin_EntryType_t replacementType ///< The type of the replacement resource.
)
//--------------------------------------------------------------------------------------------------
{
    LE_ASSERT(srcPtr->entryRef != NULL);
    LE_ASSERT(destPtr->entryRef != NULL);

    // If the destination is an Input or Output, it must be treated differently to other types
    // of resources because the app that creates it has the final authority on what its data type
    // and units are, and therefore we
    //  - don't want to clobber whatever data type and units the app has specified for its I/O, and
    //  - we have to check for data type compatibility before moving over the current value data
    //    sample (if any) to an Input or an Output.
    if ((replacementType == ADMIN_ENTRY_TYPE_INPUT) || (replacementType == ADMIN_ENTRY_TYPE_OUTPUT))
    {
        // If the old resource has a current value,
        if (srcPtr->currentValue != NULL)
        {
            // If the data type is a match for the new resource, move the current value over.
            if (srcPtr->currentType == ioPoint_GetDataType(destPtr))
            {
                destPtr->currentValue = srcPtr->currentValue;
                srcPtr->currentValue = NULL; // dest took the reference count
            }
            else  // The data type doesn't match, so drop the old resource's current value.
            {
                le_mem_Release(srcPtr->currentValue);
                srcPtr->currentValue = NULL;
            }
        }
    }
    else // *Not* an Input or Output,
    {
        // Copy over the units string.
        SetUnits(destPtr, srcPtr->units);

        // Move the current value (the new resource takes on the data type of the old resource).
        destPtr->currentType = srcPtr->currentType;
        destPtr->currentValue = srcPtr->currentValue;
        srcPtr->currentValue = NULL; // dest took the reference count
    }

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

    // Move the isConfigChanging flag.
    destPtr->isConfigChanging = srcPtr->isConfigChanging;

    // Move the push handler list.
    handler_MoveAll(&destPtr->pushHandlerList, &srcPtr->pushHandlerList);
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

    // Drop the JSON example value
    if (resPtr->jsonExample != NULL)
    {
        le_mem_Release(resPtr->jsonExample);
        resPtr->jsonExample = NULL;
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

    if (IsUpdateInProgress)
    {
        resPtr->isConfigChanging = true;
    }
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

    if (IsUpdateInProgress)
    {
        resPtr->isConfigChanging = true;
    }
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

    if (IsUpdateInProgress)
    {
        resPtr->isConfigChanging = true;
    }
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

    if (IsUpdateInProgress)
    {
        resPtr->isConfigChanging = true;
    }
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
 * Perform a transform on buffered data. Value of the observation will be the output of the
 * transform
 *
 * Ignored for all non-numeric types except Boolean for which non-zero = true and zero = false.
 */
//--------------------------------------------------------------------------------------------------
void res_SetTransform
(
    res_Resource_t* resPtr,
    admin_TransformType_t transformType,
    const double* paramsPtr,
    size_t paramsSize
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetTransform(resPtr, (obs_TransformType_t)transformType, paramsPtr, paramsSize);

    if (IsUpdateInProgress)
    {
        resPtr->isConfigChanging = true;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the type of transform currently applied to an Observation.
 *
 * @return The TransformType
 */
//--------------------------------------------------------------------------------------------------
admin_TransformType_t res_GetTransform
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetTransform(resPtr);
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
 * Mark an Output resource "optional".  (By default, they are marked "mandatory".)
 */
//--------------------------------------------------------------------------------------------------
void res_MarkOptional
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    ioPoint_MarkOptional(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Check if a given resource is a mandatory output.  If so, it means that this is an output resource
 * that must have a value before the related app function will begin working.
 *
 * @return true if a mandatory output, false if it's an optional output or not an output at all.
 */
//--------------------------------------------------------------------------------------------------
bool res_IsMandatory
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return ioPoint_IsMandatory(resPtr);
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
    if (resPtr->defaultValue != NULL)
    {
        le_mem_Release(resPtr->defaultValue);
    }

    resPtr->defaultValue = value;
    resPtr->defaultType = dataType;

    if (!IsAcceptable(resPtr, dataType))
    {
        LE_WARN("Setting default value to incompatible data type %s on resource of type %s.",
                hub_GetDataTypeName(dataType),
                hub_GetDataTypeName(resPtr->currentType));
    }
    else
    {
        // If this resource is currently operating on its default value
        // (doesn't have a compatible override or pushed value), update
        // the current value to this value.
        if (   (!res_IsOverridden(resPtr))
            && (   (resPtr->pushedValue == NULL)
                || (!IsAcceptable(resPtr, resPtr->pushedType))  )  )
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
    if (resPtr->overrideValue != NULL)
    {
        le_mem_Release(resPtr->overrideValue);
    }
    resPtr->overrideValue = value;
    resPtr->overrideType = dataType;

    if (!IsAcceptable(resPtr, dataType))
    {
        LE_WARN("Setting override to incompatible data type %s on resource of type %s.",
                hub_GetDataTypeName(dataType),
                hub_GetDataTypeName(resPtr->currentType));
    }
    else
    {
        // Update the current value to this value now.
        le_mem_AddRef(value);
        UpdateCurrentValue(resPtr, dataType, value);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Find out whether the resource currently has an override set.
 *
 * @return true if the resource has an override set, false otherwise.
 */
//--------------------------------------------------------------------------------------------------
bool res_HasOverride
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return (resPtr->overrideValue != NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Find out whether the resource currently has an override in effect.
 *
 * @return true if the resource is overridden, false otherwise.
 *
 * @note It's possible for a resource to have an override but not be overridden.  This happens when
 *       the override is the wrong data type for the resource.
 */
//--------------------------------------------------------------------------------------------------
bool res_IsOverridden
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);

    // Input and Output resources have a fixed data type, so the override is ignored
    // if the override's data type doesn't match the Input or Output resource's data type.
    // Otherwise, if the resource has an override, it is overridden.
    return (   (resPtr->overrideValue != NULL)
            && (   ((entryType != ADMIN_ENTRY_TYPE_INPUT) && (entryType != ADMIN_ENTRY_TYPE_OUTPUT))
                || (resPtr->overrideType == ioPoint_GetDataType(resPtr))  )  );
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the data type of the override value that is currently set on a given resource.
 *
 * @return The data type, or IO_DATA_TYPE_TRIGGER if not set.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t res_GetOverrideDataType
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (resPtr->overrideValue == NULL)
    {
        return IO_DATA_TYPE_TRIGGER;
    }
    return resPtr->overrideType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the override value of a resource.
 *
 * @return the override value or NULL if not set.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetOverrideValue
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->overrideValue;
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
        if ((resPtr->pushedValue != NULL) && IsAcceptable(resPtr, resPtr->pushedType))
        {
            le_mem_AddRef(resPtr->pushedValue);
            UpdateCurrentValue(resPtr, resPtr->pushedType, resPtr->pushedValue);
        }
        // Otherwise, look for a default value,
        else if ((resPtr->defaultValue != NULL) && IsAcceptable(resPtr, resPtr->defaultType))
        {
            le_mem_AddRef(resPtr->defaultValue);
            UpdateCurrentValue(resPtr, resPtr->defaultType, resPtr->defaultValue);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Notify that administrative changes are about to be performed.
 *
 * Any resource whose filter or routing (source or destination) settings are changed after a
 * call to res_StartUpdate() will stop accepting new data samples until res_EndUpdate() is called.
 * If new samples are pushed to a resource that is in this state of suspended operation, only
 * the newest one will be remembered and processed when res_EndUpdate() is called.
 */
//--------------------------------------------------------------------------------------------------
void res_StartUpdate
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    IsUpdateInProgress = true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Clear the isConfigChanging flag on a given resource.
 */
//--------------------------------------------------------------------------------------------------
static void ClearConfigChangingFlag
(
    res_Resource_t* resPtr,
    admin_EntryType_t entryType
)
//--------------------------------------------------------------------------------------------------
{
    resPtr->isConfigChanging = false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Notify that all pending administrative changes have been applied, so normal operation may resume,
 * and it's safe to delete buffer backup files that aren't being used.
 */
//--------------------------------------------------------------------------------------------------
void res_EndUpdate
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    IsUpdateInProgress = false;

    resTree_ForEachResource(ClearConfigChangingFlag);

    obs_DeleteUnusedBackupFiles();
}


//--------------------------------------------------------------------------------------------------
/**
 * Read data out of a buffer.  Data is written to a given file descriptor in JSON-encoded format
 * as an array of objects containing a timestamp and a value (or just a timestamp for triggers).
 * E.g.,
 *
 * @code
 * [{"t":1537483647.125,"v":true},{"t":1537483657.128,"v":true}]
 * @endcode
 */
//--------------------------------------------------------------------------------------------------
void res_ReadBufferJson
(
    res_Resource_t* resPtr, ///< Ptr to the resource object for the Observation.
    double startAfter,  ///< Start after this many seconds ago, or after an absolute number of
                        ///< seconds since the Epoch (if startafter > 30 years).
                        ///< Use NAN (not a number) to read the whole buffer.
    int outputFile, ///< File descriptor to write the data to.
    query_ReadCompletionFunc_t handlerPtr, ///< Completion callback.
    void* contextPtr    ///< Value to be passed to completion callback.
)
//--------------------------------------------------------------------------------------------------
{
    obs_ReadBufferJson(resPtr, startAfter, outputFile, handlerPtr, contextPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the oldest data sample held in a given Observation's buffer that is newer than a
 * given timestamp.
 *
 * @return Reference to the sample, or NULL if not found.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_FindBufferedSampleAfter
(
    res_Resource_t* resPtr, ///< Ptr to the Observation resource's object.
    double startAfter   ///< Start after this many seconds ago, or after an absolute number of
                        ///< seconds since the Epoch (if startafter > 30 years).
                        ///< Use NAN (not a number) to find the oldest.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_FindBufferedSampleAfter(resPtr, startAfter);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the JSON example value for a given resource.
 */
//--------------------------------------------------------------------------------------------------
void res_SetJsonExample
(
    res_Resource_t* resPtr,
    dataSample_Ref_t example
)
//--------------------------------------------------------------------------------------------------
{
    if (resPtr->jsonExample != NULL)
    {
        le_mem_Release(resPtr->jsonExample);
    }

    resPtr->jsonExample = example;

    // Iterate over the list of destination routes, setting their JSON example values.
    le_dls_Link_t* linkPtr = le_dls_Peek(&(resPtr->destList));
    while (linkPtr != NULL)
    {
        res_Resource_t* destPtr = CONTAINER_OF(linkPtr, res_Resource_t, destListLink);

        if (IsAcceptable(destPtr, IO_DATA_TYPE_JSON))
        {
            le_mem_AddRef(example);
            res_SetJsonExample(destPtr, example);
        }

        linkPtr = le_dls_PeekNext(&(resPtr->destList), linkPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the JSON example value for a given resource.
 *
 * @return A reference to the example value or NULL if no example set.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t res_GetJsonExample
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    return resPtr->jsonExample;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the JSON member/element specifier for extraction of data from within a structured JSON
 * value received by a given Observation.
 *
 * If this is set, all non-JSON data will be ignored, and all JSON data that does not contain the
 * the specified object member or array element will also be ignored.
 */
//--------------------------------------------------------------------------------------------------
void res_SetJsonExtraction
(
    res_Resource_t* resPtr,  ///< Observation resource.
    const char* extractionSpec    ///< [IN] string specifying the JSON member/element to extract.
)
//--------------------------------------------------------------------------------------------------
{
    obs_SetJsonExtraction(resPtr, extractionSpec);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the JSON member/element specifier for extraction of data from within a structured JSON
 * value received by a given Observation.
 *
 * @return Ptr to string containing JSON extraction specifier.  "" if not set.
 */
//--------------------------------------------------------------------------------------------------
const char* res_GetJsonExtraction
(
    res_Resource_t* resPtr  ///< Observation resource.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_GetJsonExtraction(resPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the minimum value found in an Observation's data set within a given time span.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double res_QueryMin
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_QueryMin(resPtr, startTime);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the maximum value found within a given time span in an Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double res_QueryMax
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_QueryMax(resPtr, startTime);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the mean (average) of all values found within a given time span in an Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double res_QueryMean
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_QueryMean(resPtr, startTime);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the standard deviation of all values found within a given time span in an
 * Observation's buffer.
 *
 * @return The value, or NAN (not-a-number) if there's no numerical data in the Observation's
 *         buffer (if the buffer size is zero, the buffer is empty, or the buffer contains data
 *         of a non-numerical type).
 */
//--------------------------------------------------------------------------------------------------
double res_QueryStdDev
(
    res_Resource_t* resPtr,    ///< Ptr to Observation resource.
    double startTime    ///< If < 30 years then seconds before now; else seconds since the Epoch.
)
//--------------------------------------------------------------------------------------------------
{
    return obs_QueryStdDev(resPtr, startTime);
}
