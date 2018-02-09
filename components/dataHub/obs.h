//--------------------------------------------------------------------------------------------------
/**
 * @file obs.h
 *
 * Interface definitions exposed by the Observation module to other modules within the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef OBS_H_INCLUDE_GUARD
#define OBS_H_INCLUDE_GUARD


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


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
);


#endif // OBS_H_INCLUDE_GUARD
