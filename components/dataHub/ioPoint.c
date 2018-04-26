//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the Input and Output Resources.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "dataHub.h"
#include "resource.h"
#include "ioPoint.h"


//--------------------------------------------------------------------------------------------------
/**
 * An Input or Output Resource.
 */
//--------------------------------------------------------------------------------------------------
typedef struct ioResource
{
    res_Resource_t resource;    ///< Base class. ** MUST BE FIRST **
    io_DataType_t dataType;     ///< Data type of this resource.
    le_dls_List_t pollHandlerList;  ///< List of Poll Handler callbacks the client app registered.
    bool isMandatory;   ///< true = this is a mandatory output; false otherwise.
}
IoResource_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool from which I/O Resource objects are allocated.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t IoResourcePool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Destructor for IoResource_t objects.
 */
//--------------------------------------------------------------------------------------------------
static void IoResourceDestructor
(
    void* objPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = objPtr;

// TODO: Delete poll handlers.

    res_Destruct(&ioPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the I/O Resource module.
 *
 * @warning This function MUST be called before any others in this module.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_Init
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    IoResourcePool = le_mem_CreatePool("I/O Resource", sizeof(IoResource_t));
    le_mem_SetDestructor(IoResourcePool, IoResourceDestructor);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input/Output Resource object.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
IoResource_t* Create
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = le_mem_ForceAlloc(IoResourcePool);

    res_Construct(&ioPtr->resource, entryRef);

    ioPtr->pollHandlerList = LE_DLS_LIST_INIT;

    ioPtr->dataType = dataType;
    ioPtr->isMandatory = false;

    return ioPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input Resource.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_CreateInput
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    return &(Create(dataType, entryRef)->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Output Resource.
 *
 * @return Pointer to the Resource.
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_CreateOutput
(
    io_DataType_t dataType,
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = Create(dataType, entryRef);

    ioPtr->isMandatory = true;

    return &(ioPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the data type of an Input or Output resource.
 *
 * @return The data type.
 */
//--------------------------------------------------------------------------------------------------
io_DataType_t ioPoint_GetDataType
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    return ioPtr->dataType;
}


//--------------------------------------------------------------------------------------------------
/**
 * Determine whether a value should be accepted by an Input or Output, based on data type and units.
 */
//--------------------------------------------------------------------------------------------------
bool ioPoint_ShouldAccept
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    const char* units       ///< Units string, or NULL = take on resource's units.
)
//--------------------------------------------------------------------------------------------------
{
    io_DataType_t destDataType = ioPoint_GetDataType(resPtr);

    // Check for data type mismatches.
    // Note that JSON and string type Inputs and Outputs can accept any type of data sample.
    if (   (dataType != destDataType)
        && (destDataType != IO_DATA_TYPE_STRING)
        && (destDataType != IO_DATA_TYPE_JSON) )
    {
        LE_WARN("Rejecting push: data type mismatch (pushing %s to %s).",
                hub_GetDataTypeName(dataType),
                hub_GetDataTypeName(destDataType));
        return false;
    }

    // Check for units mismatches.
    // Ignore units if the units are supposed to be obtained from the resource, or if the
    // receiving resource doesn't have units.
    if ((units != NULL) && (resPtr->units[0] != '\0') && (strcmp(units, resPtr->units) != 0))
    {
        LE_WARN("Rejecting push: units mismatch (pushing '%s' to '%s').", units, resPtr->units);
        return false;
    }

    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Mark an Output resource "optional".  (By default, they are marked "mandatory".)
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_MarkOptional
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    ioPtr->isMandatory = false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Check if a given resource is a mandatory output.  If so, it means that this is an output resource
 * that must have a value before the related app function will begin working.
 *
 * @return true if a mandatory output, false if it's an optional output or not an output at all.
 */
//--------------------------------------------------------------------------------------------------
bool ioPoint_IsMandatory
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    return ioPtr->isMandatory;
}
