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
 * Create an I/O Point Resource.
 *
 * @return Pointer to the Resource, or NULL if failed (client killed).
 */
//--------------------------------------------------------------------------------------------------
res_Resource_t* ioPoint_Create
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
 * Perform processing of an accepted pushed data sample that is specific to an Input or Output
 * resource.
 */
//--------------------------------------------------------------------------------------------------
void ioPoint_ProcessAccepted
(
    res_Resource_t* resPtr,
    io_DataType_t dataType,     ///< Data type of the data sample.
    dataSample_Ref_t sampleRef  ///< Data sample.
);


#endif // IO_POINT_H_INCLUDE_GUARD
