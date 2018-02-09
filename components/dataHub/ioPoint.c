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
    le_dls_List_t pushHandlerList;  ///< List of Push Handler callbacks the client app registered.
}
IoResource_t;


//--------------------------------------------------------------------------------------------------
/**
 * Holds the details of a Handler callback that has been registered by a client app.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_dls_Link_t link; ///< Used to link into one of the I/O resource's lists of handlers.
    void* safeRef;      ///< Safe reference passed to client.
    IoResource_t* ioResPtr; ///< Ptr to the I/O Resource this handler is registered with.
    io_DataType_t dataType;    ///< Data type of the handler callback (only for Push handlers).
    void* callbackPtr;  ///< The callback function pointer.
    void* contextPtr;   ///< The context pointer provided by the client.
}
Handler_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool from which I/O Resource objects are allocated.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t IoResourcePool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Pool from which Handler objects are allocated.
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t HandlerPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Safe reference map for Handler objects.  Used to generate safe references to pass to clients
 * when they register Poll and Push handler call-backs.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t HandlerRefMap = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Delete a Handler object.
 */
//--------------------------------------------------------------------------------------------------
static void DeleteHandler
(
    Handler_t* handlerPtr
)
//--------------------------------------------------------------------------------------------------
{
    le_ref_DeleteRef(HandlerRefMap, handlerPtr->safeRef);
    handlerPtr->safeRef = NULL;

    le_mem_Release(handlerPtr);
}


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

    le_dls_Link_t* linkPtr;

    while ((linkPtr = le_dls_Pop(&ioPtr->pollHandlerList)) != NULL)
    {
        DeleteHandler(CONTAINER_OF(linkPtr, Handler_t, link));
    }

    while ((linkPtr = le_dls_Pop(&ioPtr->pushHandlerList)) != NULL)
    {
        DeleteHandler(CONTAINER_OF(linkPtr, Handler_t, link));
    }

    res_Destruct(&ioPtr->resource);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an Input/Output Resource in a given namespace.
 *
 * @return Pointer to the Resource, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
static IoResource_t* CreateIo
(
    io_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = le_mem_ForceAlloc(IoResourcePool);

    ioPtr->pollHandlerList = LE_DLS_LIST_INIT;
    ioPtr->pushHandlerList = LE_DLS_LIST_INIT;

    ioPtr->dataType = dataType;

    return ioPtr;
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

    HandlerPool = le_mem_CreatePool("I/O Handler", sizeof(Handler_t));

    HandlerRefMap = le_ref_CreateMap("I/O Handler", 20 /* totally arbitrary; make configurable */);
}


//--------------------------------------------------------------------------------------------------
/**
 * Create an I/O Point Resource.
 *
 * @return Pointer to the Resource, or NULL if failed (client killed).
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_Create
(
    io_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    return &(CreateIo(dataType)->resource);
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
 * Add a Push Handler.
 *
 * @return Reference to the handler added.
 */
//--------------------------------------------------------------------------------------------------
hub_HandlerRef_t ioPoint_AddPushHandler
(
    res_Resource_t* resPtr, ///< Ptr to the I/O Point Resource.
    io_DataType_t dataType,
    void* callbackPtr,
    void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
    admin_EntryType_t entryType = resTree_GetEntryType(resPtr->entryRef);
    LE_ASSERT((entryType == ADMIN_ENTRY_TYPE_INPUT) || (entryType == ADMIN_ENTRY_TYPE_OUTPUT));

    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    Handler_t* handlerPtr = le_mem_ForceAlloc(HandlerPool);

    handlerPtr->link = LE_DLS_LINK_INIT;
    handlerPtr->safeRef = le_ref_CreateRef(HandlerRefMap, handlerPtr);
    handlerPtr->ioResPtr = ioPtr;
    handlerPtr->dataType = dataType;
    handlerPtr->callbackPtr = callbackPtr;
    handlerPtr->contextPtr = contextPtr;

    le_dls_Queue(&ioPtr->pushHandlerList, &handlerPtr->link);

    return (hub_HandlerRef_t)(handlerPtr->safeRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Remove a Push Handler.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_RemovePushHandler
(
    hub_HandlerRef_t handlerRef
)
//--------------------------------------------------------------------------------------------------
{
    Handler_t* handlerPtr = le_ref_Lookup(HandlerRefMap, handlerRef);

    if (handlerPtr != NULL)
    {
        le_dls_Remove(&handlerPtr->ioResPtr->pushHandlerList, &handlerPtr->link);
        DeleteHandler(handlerPtr);
    }
    else
    {
        LE_ERROR("Invalid handler reference %p", handlerRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform processing of an accepted pushed data sample that is specific to an Input or Output
 * resource.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_ProcessAccepted
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< Data type of the data sample.
    dataSample_Ref_t sampleRef  ///< Data sample.
)
//--------------------------------------------------------------------------------------------------
{
    IoResource_t* ioPtr = CONTAINER_OF(resPtr, IoResource_t, resource);

    // Iterate over the Push Handler List and call each one that is a data type match.
    le_dls_Link_t* linkPtr = le_dls_Peek(&ioPtr->pushHandlerList);
    while (linkPtr != NULL)
    {
        Handler_t* handlerPtr = CONTAINER_OF(linkPtr, Handler_t, link);

        if (handlerPtr->dataType == dataType)
        {
            double timestamp = dataSample_GetTimestamp(sampleRef);

            switch (dataType)
            {
                case IO_DATA_TYPE_TRIGGER:
                {
                    io_TriggerPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                    callbackPtr(timestamp, handlerPtr->contextPtr);
                    break;
                }

                case IO_DATA_TYPE_BOOLEAN:
                {
                    io_BooleanPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                    callbackPtr(timestamp,
                                dataSample_GetBoolean(sampleRef),
                                handlerPtr->contextPtr);
                    break;
                }

                case IO_DATA_TYPE_NUMERIC:
                {
                    io_NumericPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                    callbackPtr(timestamp,
                                dataSample_GetNumeric(sampleRef),
                                handlerPtr->contextPtr);
                    break;
                }

                case IO_DATA_TYPE_STRING:
                {
                    io_StringPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                    callbackPtr(timestamp,
                                dataSample_GetString(sampleRef),
                                handlerPtr->contextPtr);
                    break;
                }

                case IO_DATA_TYPE_JSON:
                {
                    io_JsonPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                    callbackPtr(timestamp,
                                dataSample_GetJson(sampleRef),
                                handlerPtr->contextPtr);
                    break;
                }
            }
        }
        else if (handlerPtr->dataType == IO_DATA_TYPE_STRING)
        {
            char value[IO_MAX_STRING_VALUE_LEN];
            if (LE_OK != dataSample_ConvertToString(sampleRef,
                                                    res_GetDataType(resPtr),
                                                    value,
                                                    sizeof(value)) )
            {
                LE_ERROR("Conversion to string would result in string buffer overflow.");
            }
            else
            {
                double timestamp = dataSample_GetTimestamp(sampleRef);

                io_StringPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                callbackPtr(timestamp,
                            value,
                            handlerPtr->contextPtr);
            }
        }
        else if (handlerPtr->dataType == IO_DATA_TYPE_JSON)
        {
            char value[IO_MAX_STRING_VALUE_LEN];
            if (LE_OK != dataSample_ConvertToJson(sampleRef,
                                                  res_GetDataType(resPtr),
                                                  value,
                                                  sizeof(value)) )
            {
                LE_ERROR("Conversion to JSON would result in string buffer overflow.");
            }
            else
            {
                double timestamp = dataSample_GetTimestamp(sampleRef);

                io_JsonPushHandlerFunc_t callbackPtr = handlerPtr->callbackPtr;
                callbackPtr(timestamp,
                            value,
                            handlerPtr->contextPtr);
            }
        }

        linkPtr = le_dls_PeekNext(&ioPtr->pushHandlerList, linkPtr);
    }
}
