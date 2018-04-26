//--------------------------------------------------------------------------------------------------
/**
 * Interface provided by the I/O Point module to other modules within the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef IO_POINT_H_INCLUDE_GUARD
#define IO_POINT_H_INCLUDE_GUARD


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the I/O Point module.  This function MUST be called before any others in this module.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_Init
(
    void
);


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
);


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
);


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
);


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
);


//--------------------------------------------------------------------------------------------------
/**
 * Add a Push Handler.
 *
 * @return Reference to the handler added.
 */
//--------------------------------------------------------------------------------------------------
hub_HandlerRef_t ioPoint_AddPushHandler
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,
    void* callbackPtr,
    void* contextPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Remove a Push Handler.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_RemovePushHandler
(
    hub_HandlerRef_t handlerRef
);


//--------------------------------------------------------------------------------------------------
/**
 * Mark an Output resource "optional".  (By default, they are marked "mandatory".)
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_MarkOptional
(
    res_Resource_t* resPtr
);


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
);


#endif // IO_POINT_H_INCLUDE_GUARD
