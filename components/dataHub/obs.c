//--------------------------------------------------------------------------------------------------
/**
 * @file obs.c
 *
 * Implementation of Observations.
 *
 * The data sample buffer backup file format looks like this (little-endian byte order):
 *
 * - file format version byte = 0
 * - data type byte containing one of the following ASCII characters:
 *       t = trigger
 *       b = Boolean
 *       n = numeric
 *       s = string
 *       j = JSON
 * - number of records = 4-byte unsigned integer
 * - array of records, sorted oldest-first, each containing:
 *       - timestamp (8-byte IEEE double-precision floating point value)
 *       - value, depending on data type, as follows:
 *             t -> no value
 *             b -> 1 byte, 0 = false, 1 = true
 *             n -> 8-byte IEEE double-precision floating point value
 *             s -> 4-byte unsigned integer length, followed by string content (no null-terminator)
 *             j -> 4-byte unsigned integer length, followed by JSON string content (no term char)
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


#define BACKUP_DIR "backup/"
#define BACKUP_SUFFIX ".bak"
#define MAX_BACKUP_FILE_PATH_BYTES (  sizeof(BACKUP_DIR) \
                                    + IO_MAX_RESOURCE_PATH_LEN \
                                    + sizeof(BACKUP_SUFFIX)  )

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

    io_DataType_t bufferedType; ///< Data type of samples currently in the buffer.

    uint32_t backupPeriod; ///< Min time (in seconds) between non-volatile backups of the buffer.
    uint32_t lastBackupTime; ///< Time at which last push was accepted (seconds, relative clock).
    le_timer_Ref_t backupTimer; ///< Reference to the timer used to trigger the next backup.

    le_sls_List_t sampleList; ///< Queue of buffered data samples (oldest first, newest last).
}
Observation_t;


/// Object used to link a Data Sample into an Observation's buffer.
/// Holds a reference on the Data Sample object.
typedef struct
{
    le_sls_Link_t link;  ///< Used to link into a Observation's sampleList.
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

    // Delete all the buffered data samples.
    le_sls_Link_t* linkPtr;
    while (NULL != (linkPtr = le_sls_Pop(&obsPtr->sampleList)))
    {
        BufferEntry_t* buffEntryPtr = CONTAINER_OF(linkPtr, BufferEntry_t, link);

        le_mem_Release(buffEntryPtr);
    }

    obsPtr->count = 0;
    obsPtr->maxCount = 0;

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

    le_mem_Release(buffEntryPtr->sampleRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Adds a given data sample to the buffer of a given Observation.
 */
//--------------------------------------------------------------------------------------------------
static void AddToBuffer
(
    Observation_t* obsPtr,
    dataSample_Ref_t sampleRef
)
//--------------------------------------------------------------------------------------------------
{
    BufferEntry_t* buffEntryPtr = le_mem_ForceAlloc(BufferEntryPool);

    le_mem_AddRef(sampleRef);
    buffEntryPtr->sampleRef = sampleRef;
    buffEntryPtr->link = LE_SLS_LINK_INIT;
    le_sls_Queue(&obsPtr->sampleList, &buffEntryPtr->link);

    (obsPtr->count)++;
}


//--------------------------------------------------------------------------------------------------
/**
 * If the number of entries in a given Observation's buffer is larger than the number given,
 * discard enough of the oldest entries to correct that condition.
 */
//--------------------------------------------------------------------------------------------------
static void TruncateBuffer
(
    Observation_t* obsPtr,
    size_t count
)
//--------------------------------------------------------------------------------------------------
{
    while (obsPtr->count > count)
    {
        le_sls_Link_t* linkPtr = le_sls_Pop(&obsPtr->sampleList);

        le_mem_Release(CONTAINER_OF(linkPtr, BufferEntry_t, link));

        (obsPtr->count)--;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the file system path to use for the backup file for a given Observation's data sample buffer.
 *
 * @return LE_OK if successful.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetBackupFilePath
(
    char* pathBuffPtr,  ///< [OUT] Ptr to where the path will be written.
    size_t pathBuffSize,    ///< Size of the buffer in bytes.
    Observation_t* obsPtr
)
//--------------------------------------------------------------------------------------------------
{
    size_t len;
    LE_ASSERT(LE_OK == le_utf8_Copy(pathBuffPtr, BACKUP_DIR, pathBuffSize, &len));

    pathBuffPtr += len;
    pathBuffSize -= len;

    resTree_EntryRef_t obsNamespace = resTree_FindEntry(resTree_GetRoot(), "obs");
    LE_ASSERT(obsNamespace != NULL);

    ssize_t result = resTree_GetPath(pathBuffPtr,
                                     pathBuffSize,
                                     obsNamespace,
                                     res_GetResTreeEntry(&obsPtr->resource));
    if (result <= 0)
    {
        LE_CRIT("Failed to fetch Observation path for '%s' (%s).",
                resTree_GetEntryName(res_GetResTreeEntry(&obsPtr->resource)),
                LE_RESULT_TXT(result));

        return result;
    }

    pathBuffPtr += result;
    pathBuffSize -= result;

    return le_utf8_Copy(pathBuffPtr, BACKUP_SUFFIX, pathBuffSize, NULL);
}


//--------------------------------------------------------------------------------------------------
/**
 * Writes a buffer load of data to a backup file.
 *
 * On error, logs an error message and closes the file.
 *
 * @return true if successful, false if failed.
 */
//--------------------------------------------------------------------------------------------------
static bool WriteToFile
(
    FILE* file,
    const void* buffPtr,
    size_t buffSize
)
//--------------------------------------------------------------------------------------------------
{
    size_t recordsWritten = fwrite(buffPtr, buffSize, 1, file);
    if (recordsWritten != 1)
    {
        LE_CRIT("Failed to write (%m).");
        le_atomFile_CancelStream(file);
        return false;
    }
    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Reads a buffer load of data from a backup file.
 *
 * On error, logs an error message and closes the file.
 *
 * @return LE_OK if successful, LE_UNDERFLOW if the end of file was reached, LE_FAULT if failed.
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ReadFromFile
(
    void* buffPtr,
    size_t buffSize,
    FILE* file
)
//--------------------------------------------------------------------------------------------------
{
    size_t bytesRead = fread(buffPtr, 1, buffSize, file);

    le_result_t result = LE_OK;

    if (bytesRead < buffSize)
    {
        if (feof(file))
        {
            result = LE_UNDERFLOW;
        }
        else
        {
            LE_CRIT("Failed to read (%m).");
            result = LE_FAULT;
        }

        le_atomFile_CancelStream(file);
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the data type code byte to be written into a backup file.
 *
 * @return the data type code byte.
 */
//--------------------------------------------------------------------------------------------------
static uint8_t GetDataTypeCode
(
    Observation_t* obsPtr
)
//--------------------------------------------------------------------------------------------------
{
    switch (res_GetDataType(&obsPtr->resource))
    {
        case IO_DATA_TYPE_TRIGGER:  return 't';
        case IO_DATA_TYPE_BOOLEAN:  return 'b';
        case IO_DATA_TYPE_NUMERIC:  return 'n';
        case IO_DATA_TYPE_STRING:   return 's';
        case IO_DATA_TYPE_JSON:     return 'j';
    }

    LE_FATAL("Invalid data type %d.", res_GetDataType(&obsPtr->resource));
}


//--------------------------------------------------------------------------------------------------
/**
 * Gets the data type represented by the code byte read from a backup file.
 *
 * @return true if successful, false on error.
 */
//--------------------------------------------------------------------------------------------------
static bool GetDataTypeFromCode
(
    io_DataType_t* dataTypePtr, ///< [OUT] Ptr to where the result will be put on success.
    uint8_t code
)
//--------------------------------------------------------------------------------------------------
{
    switch (code)
    {
        case 't': *dataTypePtr = IO_DATA_TYPE_TRIGGER; return true;
        case 'b': *dataTypePtr = IO_DATA_TYPE_BOOLEAN; return true;
        case 'n': *dataTypePtr = IO_DATA_TYPE_NUMERIC; return true;
        case 's': *dataTypePtr = IO_DATA_TYPE_STRING;  return true;
        case 'j': *dataTypePtr = IO_DATA_TYPE_JSON;    return true;
    }

    LE_CRIT("Invalid data type code %d.", (int)code);

    return false;
}


//--------------------------------------------------------------------------------------------------
/**
 * Writes all the data samples for a given Observation to a given backup file.
 *
 * On error, logs an error message and closes the file.
 *
 * @return true if successful, false if failed.
 */
//--------------------------------------------------------------------------------------------------
static bool WriteSamplesToFile
(
    FILE* file,
    Observation_t* obsPtr
)
//--------------------------------------------------------------------------------------------------
{
    le_sls_Link_t* linkPtr = le_sls_Peek(&obsPtr->sampleList);

    io_DataType_t dataType = obsPtr->bufferedType;

    while (linkPtr != NULL)
    {
        BufferEntry_t* buffEntryPtr = CONTAINER_OF(linkPtr, BufferEntry_t, link);

        // Write the timestamp.
        double timestamp = dataSample_GetTimestamp(buffEntryPtr->sampleRef);
        if (!WriteToFile(file, &timestamp, sizeof(timestamp)))
        {
            return false;
        }

        switch (dataType)
        {
            case IO_DATA_TYPE_TRIGGER:

                // No Value.
                break;

            case IO_DATA_TYPE_BOOLEAN:
            {
                bool value = dataSample_GetBoolean(buffEntryPtr->sampleRef);
                if (!WriteToFile(file, &value, sizeof(value)))
                {
                    return false;
                }
                break;
            }
            case IO_DATA_TYPE_NUMERIC:
            {
                double value = dataSample_GetNumeric(buffEntryPtr->sampleRef);
                if (!WriteToFile(file, &value, sizeof(value)))
                {
                    return false;
                }
                break;
            }
            case IO_DATA_TYPE_STRING:
            {
                const char* valuePtr = dataSample_GetString(buffEntryPtr->sampleRef);
                uint32_t stringLen = strlen(valuePtr);
                if (!WriteToFile(file, &stringLen, 4))
                {
                    return false;
                }
                if (!WriteToFile(file, valuePtr, stringLen))
                {
                    return false;
                }
                break;
            }
            case IO_DATA_TYPE_JSON:
            {
                const char* valuePtr = dataSample_GetJson(buffEntryPtr->sampleRef);
                uint32_t stringLen = strlen(valuePtr);
                if (!WriteToFile(file, &stringLen, 4))
                {
                    return false;
                }
                if (!WriteToFile(file, valuePtr, stringLen))
                {
                    return false;
                }
                break;
            }
        }

        linkPtr = le_sls_PeekNext(&obsPtr->sampleList, linkPtr);
    }

    return true;
}


//--------------------------------------------------------------------------------------------------
/**
 * Reads all the data samples from a given backup file and adds them to a given Observation's
 * data sample buffer.  Closes the file when done.
 */
//--------------------------------------------------------------------------------------------------
static void ReadSamplesFromFile
(
    Observation_t* obsPtr,
    FILE* file,
    size_t count    ///< The expected number of samples to read.
)
//--------------------------------------------------------------------------------------------------
{
    if (count == 0)
    {
        return;
    }

    io_DataType_t dataType = obsPtr->bufferedType;

    dataSample_Ref_t dataSample = NULL;

    for (;;)
    {
        // Read the timestamp.
        double timestamp;
        le_result_t result = ReadFromFile(&timestamp, sizeof(timestamp), file);
        if (result != LE_OK)
        {
            if (result == LE_UNDERFLOW)
            {
                // End of file reached, check that we've received all we should.
                if (count == 0)
                {
                    // The file contained exactly the number of samples we expected.
                    // The last data sample read from the file (which is the newest)
                    // should be pushed to the Observation so it becomes the current value.
                    res_Push(&obsPtr->resource, dataType, "", dataSample);
                    return;
                }
                else
                {
                    LE_CRIT("Backup file was truncated. Expected %zu more samples.", count);
                }
            }

            goto error;
        }

        // We succeeded in reading another timestamp, so we had better be expecting more samples.
        if (count == 0)
        {
            LE_CRIT("Backup file contains extra samples.");
            le_atomFile_CancelStream(file);
            goto error;
        }

        count--;

        switch (dataType)
        {
            case IO_DATA_TYPE_TRIGGER:

                // No Value.
                dataSample = dataSample_CreateTrigger(timestamp);
                break;

            case IO_DATA_TYPE_BOOLEAN:
            {
                bool value;
                if (ReadFromFile(&value, sizeof(value), file) != LE_OK)
                {
                    LE_CRIT("Failed to read boolean value.");
                    goto error;
                }
                dataSample = dataSample_CreateBoolean(timestamp, value);
                break;
            }
            case IO_DATA_TYPE_NUMERIC:
            {
                double value;
                if (ReadFromFile(&value, sizeof(value), file) != LE_OK)
                {
                    LE_CRIT("Failed to read numeric value.");
                    goto error;
                }
                dataSample = dataSample_CreateNumeric(timestamp, value);
                break;
            }
            case IO_DATA_TYPE_STRING:
            {
                char value[IO_MAX_STRING_VALUE_LEN + 1];

                uint32_t stringLen;
                if (ReadFromFile(&stringLen, 4, file) != LE_OK)
                {
                    LE_CRIT("Failed to read string length.");
                    goto error;
                }
                if (stringLen > (sizeof(value) - 1))
                {
                    LE_CRIT("String length (%zu) is larger than permitted (%zu).",
                            (size_t)stringLen,
                            sizeof(value) - 1);
                    le_atomFile_CancelStream(file);
                    goto error;
                }
                if (ReadFromFile(value, stringLen, file) != LE_OK)
                {
                    LE_CRIT("Failed to read string value of length %zu.", (size_t)stringLen);
                    goto error;
                }
                value[stringLen] = '\0';
                dataSample = dataSample_CreateString(timestamp, value);
                break;
            }
            case IO_DATA_TYPE_JSON:
            {
                char value[IO_MAX_STRING_VALUE_LEN + 1];

                uint32_t stringLen;
                if (ReadFromFile(&stringLen, 4, file) != LE_OK)
                {
                    LE_CRIT("Failed to read JSON object length.");
                    goto error;
                }
                if (stringLen > (sizeof(value) - 1))
                {
                    LE_CRIT("JSON string length (%zu) is larger than permitted (%zu).",
                            (size_t)stringLen,
                            sizeof(value) - 1);
                    le_atomFile_CancelStream(file);
                    goto error;
                }
                if (ReadFromFile(value, stringLen, file) != LE_OK)
                {
                    LE_CRIT("Failed to read JSON value of length %zu.", (size_t)stringLen);
                    goto error;
                }
                value[stringLen] = '\0';
                dataSample = dataSample_CreateJson(timestamp, value);
                break;
            }
        }

        // Add the sample to the buffer, unless this is the last (newest) sample, in which case
        // we will push it to the Observation later when we confirm that the file doesn't have
        // more than expected in it (which would mean that all these samples are probably corrupt
        // and need to be discarded).
        if (count != 0)
        {
            AddToBuffer(obsPtr, dataSample);
        }
    }

error:

    // On error, dump the buffer contents in case we read some corrupted samples from the file.
    TruncateBuffer(obsPtr, 0);
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform a backup to non-volatile storage of an observation's data sample buffer.
 */
//--------------------------------------------------------------------------------------------------
static void Backup
(
    Observation_t* obsPtr
)
//--------------------------------------------------------------------------------------------------
{
    LE_DEBUG("Backing up...");

    // If the backup timer exists, delete it.
    if (obsPtr->backupTimer != NULL)
    {
        le_timer_Delete(obsPtr->backupTimer);
        obsPtr->backupTimer = NULL;
    }

    // Update the time of last backup.
    le_clk_Time_t now = le_clk_GetRelativeTime();
    obsPtr->lastBackupTime = now.sec;

    // Get the backup file path.
    char path[MAX_BACKUP_FILE_PATH_BYTES];
    if (GetBackupFilePath(path, sizeof(path), obsPtr) != LE_OK)
    {
        return;
    }

    // Create the backup directory, if it doesn't exist already.
    struct stat st = {0};
    if (stat(BACKUP_DIR, &st) == -1)
    {
        LE_DEBUG("Creating directory '" BACKUP_DIR "'.");

        if (mkdir(BACKUP_DIR, 0700) == -1)
        {
            LE_CRIT("Unable to create directory '" BACKUP_DIR "' (%m).");
            return;
        }
    }

    // Open the file for writing, truncating it to zero length to start.
    le_result_t result;
    FILE* file = le_atomFile_CreateStream(path,
                                          LE_FLOCK_WRITE,
                                          LE_FLOCK_REPLACE_IF_EXIST,
                                          0600,
                                          &result);
    if (result != LE_OK)
    {
        LE_CRIT("Unable to open file '%s' for writing (%s).", path, LE_RESULT_TXT(result));
        return;
    }

    // Write in the version byte.
    uint8_t byte = 0;
    if (!WriteToFile(file, &byte, 1))
    {
        return;
    }

    // Write the data type code.
    byte = GetDataTypeCode(obsPtr);
    if (byte == 0)
    {
        le_atomFile_CancelStream(file);
        return;
    }
    if (!WriteToFile(file, &byte, 1))
    {
        return;
    }

    // Write in the number of samples.
    uint32_t count = obsPtr->count;
    if (!WriteToFile(file, &count, 4))
    {
        return;
    }

    // Write all the data samples to the file.
    if (!WriteSamplesToFile(file, obsPtr))
    {
        return;
    }

    // Commit the file.
    result = le_atomFile_CloseStream(file);
    if (result != LE_OK)
    {
        LE_CRIT("Failed to save '%s' (%s).", path, LE_RESULT_TXT(result));
    }

    LE_DEBUG("Backup complete.");
}


//--------------------------------------------------------------------------------------------------
/**
 * Disable backups of a given Observation's data sample buffer.
 */
//--------------------------------------------------------------------------------------------------
static void DisableBackups
(
    Observation_t* obsPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (obsPtr->backupTimer != NULL)
    {
        le_timer_Stop(obsPtr->backupTimer);
        le_timer_Delete(obsPtr->backupTimer);
        obsPtr->backupTimer = NULL;
    }

    obsPtr->lastBackupTime = 0;

    char path[MAX_BACKUP_FILE_PATH_BYTES];
    if (GetBackupFilePath(path, sizeof(path), obsPtr) == LE_OK)
    {
        LE_INFO("TODO: DELETE BACKUP at %s", path);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Timer expiry handler function for Observation data sample buffer backup timers.
 *
 * Saves the contents of an Observation's data sample buffer to non-volatile storage.
 *
 * @note The timer should only be running if a data sample arrived in the buffer before the
 *       backup period had elapsed since the previous backup.
 */
//--------------------------------------------------------------------------------------------------
static void BackupTimerExpired
(
    le_timer_Ref_t timer
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = le_timer_GetContextPtr(timer);

    Backup(obsPtr);
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
    resTree_EntryRef_t entryRef ///< The resource tree entry to attach this Resource to.
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = le_mem_ForceAlloc(ObservationPool);

    res_Construct(&obsPtr->resource, entryRef);

    obsPtr->lowLimit = NAN;
    obsPtr->highLimit = NAN;
    obsPtr->changeBy = NAN;
    obsPtr->minPeriod = NAN;

    obsPtr->maxCount = 0;
    obsPtr->count = 0;

    obsPtr->bufferedType = IO_DATA_TYPE_TRIGGER;

    obsPtr->backupPeriod = 0;
    obsPtr->lastBackupTime = 0;
    obsPtr->backupTimer = NULL;

    obsPtr->sampleList = LE_SLS_LIST_INIT;

    return &obsPtr->resource;
}


//--------------------------------------------------------------------------------------------------
/**
 * Restore an Observation's data buffer from non-volatile backup, if one exists.
 */
//--------------------------------------------------------------------------------------------------
void obs_RestoreBackup
(
    res_Resource_t* resPtr
)
//--------------------------------------------------------------------------------------------------
{
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    LE_DEBUG("Restoring backup...");

    char path[MAX_BACKUP_FILE_PATH_BYTES];
    if (GetBackupFilePath(path, sizeof(path), obsPtr) != LE_OK)
    {
        return;
    }

    // Open the file for reading.
    le_result_t result;
    FILE* file = le_atomFile_OpenStream(path, LE_FLOCK_READ, &result);
    if (result != LE_OK)
    {
        LE_DEBUG("Unable to open '%s' for reading (%s).", path, LE_RESULT_TXT(result));
        return;
    }

    // Read the version byte.
    uint8_t byte;
    if (ReadFromFile(&byte, 1, file) != LE_OK)
    {
        LE_ERROR("Failed to read version byte.");
        return;
    }
    if (byte != 0)
    {
        LE_CRIT("Backup file format version %d unrecognized.", (int)byte);
        le_atomFile_CancelStream(file);
        return;
    }

    // Read the data type code.
    if (ReadFromFile(&byte, 1, file) != LE_OK)
    {
        LE_ERROR("Failed to read data type code.");
        return;
    }
    io_DataType_t dataType;
    if (!GetDataTypeFromCode(&dataType, byte))
    {
        le_atomFile_CancelStream(file);
        return;
    }
    obsPtr->bufferedType = dataType;

    // Read the number of samples.
    uint32_t count;
    if (ReadFromFile(&count, 4, file) != LE_OK)
    {
        LE_ERROR("Failed to read number of samples.");
        return;
    }

    // The maximum count must be at least the number we read.
    if (obsPtr->maxCount == 0)
    {
        obsPtr->maxCount = count;
    }
    // NOTE: Don't enable backups, though, because we don't know the frequency to choose
    //       and flash wear can permanently damage a device.

    // Read all the data samples from the file.
    ReadSamplesFromFile(obsPtr, file, count);
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
    Observation_t* obsPtr = CONTAINER_OF(resPtr, Observation_t, resource);

    if (obsPtr->maxCount > 0)
    {
        // If the data type has changed, we have to dump the current set of buffered samples.
        if (obsPtr->bufferedType != dataType)
        {
            TruncateBuffer(obsPtr, 0);

            obsPtr->bufferedType = dataType;
        }

        AddToBuffer(obsPtr, sampleRef);

        TruncateBuffer(obsPtr, obsPtr->maxCount);

        // If the buffer backup period is non-zero, then back-ups are enabled.
        if (obsPtr->backupPeriod > 0)
        {
            // If more than the backup period has passed since the time of last backup, do a backup.
            uint32_t nextBackupTime = obsPtr->lastBackupTime + obsPtr->backupPeriod;
            le_clk_Time_t now = le_clk_GetRelativeTime();
            if (nextBackupTime <= now.sec)
            {
                Backup(obsPtr);
            }
            // If the backup period hasn't passed yet, and there isn't already a timer running,
            // then start a timer to expire when it's time to do a backup.
            else if (obsPtr->backupTimer == NULL)
            {
                uint32_t timerInterval = (nextBackupTime - now.sec) * 1000;

                obsPtr->backupTimer = le_timer_Create("backup");
                LE_ASSERT(le_timer_SetMsInterval(obsPtr->backupTimer, timerInterval) == LE_OK);
                LE_ASSERT(le_timer_SetHandler(obsPtr->backupTimer, BackupTimerExpired) == LE_OK);
                LE_ASSERT(le_timer_SetContextPtr(obsPtr->backupTimer, obsPtr) == LE_OK);
                LE_ASSERT(le_timer_Start(obsPtr->backupTimer) == LE_OK);
            }
        }
    }
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

    // If the buffer size is being changed,
    if (obsPtr->maxCount != count)
    {
        // If the size is now zero and backups were enabled, disable backups.
        if ((count == 0) && (obsPtr->backupPeriod > 0))
        {
            DisableBackups(obsPtr);
        }

        // Update the size.
        obsPtr->maxCount = count;

        // Discard extra samples if the size has shrunk.
        TruncateBuffer(obsPtr, count);
    }
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

    uint32_t oldPeriod = obsPtr->backupPeriod;

    // If the period is being changed,
    if (oldPeriod != seconds)
    {
        obsPtr->backupPeriod = seconds;

        // If the buffer size is zero, then backups aren't done, so we can skip the rest.
        if (obsPtr->maxCount > 0)
        {
            // If the period is now zero, disable backups.
            if (seconds == 0)
            {
                DisableBackups(obsPtr);
            }
            // If there's nothing in the buffer, we can skip the rest and just wait for something
            // to be added to the buffer.
            else if (!le_sls_IsEmpty(&obsPtr->sampleList))
            {
                // If backups were already enabled and the period has just changed,
                if (oldPeriod != 0)
                {
                    // If the timer is running, then we know there's something waiting to be
                    // backed up.  Otherwise, we wait for something to be added to the buffer.
                    if (obsPtr->backupTimer != NULL)
                    {
                        // Stop the old timer, because we know it has the wrong interval.
                        le_timer_Stop(obsPtr->backupTimer);

                        // If the backup period has passed since the last backup,
                        // release the timer and do a backup now.
                        uint32_t nextBackupTime = obsPtr->lastBackupTime + seconds;
                        le_clk_Time_t now = le_clk_GetRelativeTime();
                        if (nextBackupTime < now.sec)
                        {
                            le_timer_Delete(obsPtr->backupTimer);
                            obsPtr->backupTimer = NULL;

                            Backup(obsPtr);
                        }
                        else // If the backup period has not yet passed, correct the timer's
                             // interval and restart it.
                        {
                            uint32_t timerInterval = (nextBackupTime - now.sec) * 1000;
                            LE_ASSERT(le_timer_SetMsInterval(obsPtr->backupTimer, timerInterval)
                                      == LE_OK);
                            LE_ASSERT(le_timer_Start(obsPtr->backupTimer) == LE_OK);
                        }
                    }
                }
            }
        }
    }
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
