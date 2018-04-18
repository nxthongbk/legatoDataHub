//--------------------------------------------------------------------------------------------------
/**
 * Implementation of the "dhub" command-line tool for administering the Data Hub from the
 * command line.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "interfaces.h"


//--------------------------------------------------------------------------------------------------
/**
 * What type of action are we being asked to do?
 */
//--------------------------------------------------------------------------------------------------
static enum
{
    ACTION_UNSPECIFIED,
    ACTION_HELP,
    ACTION_LIST,
    ACTION_GET,
    ACTION_SET,
    ACTION_REMOVE,
    ACTION_PUSH,
    ACTION_QUERY,
    ACTION_POLL,
    ACTION_READ,
}
Action = ACTION_UNSPECIFIED;


//--------------------------------------------------------------------------------------------------
/**
 * What type of object are we being asked to act on?
 */
//--------------------------------------------------------------------------------------------------
static enum
{
    OBJECT_SOURCE,
    OBJECT_DEFAULT,
    OBJECT_OVERRIDE,
    OBJECT_MIN_PERIOD,
    OBJECT_LOW_LIMIT,
    OBJECT_HIGH_LIMIT,
    OBJECT_CHANGE_BY,
    OBJECT_BUFFER_SIZE,
    OBJECT_BACKUP_PERIOD,
    OBJECT_OBSERVATION,
}
Object;


//--------------------------------------------------------------------------------------------------
/**
 * Print help text to stdout and exit with EXIT_SUCCESS.
 */
//--------------------------------------------------------------------------------------------------
static void HandleHelpRequest
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    puts(
        "NAME:\n"
        "    dhub - Data Hub administration tool.\n"
        "\n"
        "SYNOPSIS:\n"
        "    dhub list [PATH]\n"
        "    dhub set source PATH SRC_PATH\n"
        "    dhub set default PATH VALUE\n"
        "    dhub set override PATH VALUE\n"
        "    dhub set minPeriod PATH\n"
        "    dhub set lowLimit PATH\n"
        "    dhub set highLimit PATH\n"
        "    dhub set changeBy PATH\n"
        "    dhub set bufferSize PATH\n"
        "    dhub set backupPeriod PATH\n"
        "    dhub remove OBJECT PATH\n"
        "    dhub get OBJECT PATH\n"
//        "    dhub push PATH VALUE\n"
//        "    dhub query PATH QUERY [--start=START]\n"
//        "    dhub poll PATH\n"
//        "    dhub read PATH [--start=START]\n"
        "    dhub help\n"
        "    dhub -h\n"
        "    dhub --help\n"
        "\n"
        "DESCRIPTION:\n"
        "    dhub list [PATH]\n"
        "            Lists all existing resources under PATH.\n"
        "            If PATH is not specified, the default is '/'.\n"
        "\n"
        "    dhub set source PATH SRC_PATH\n"
        "            Sets the data flow source of the resource at PATH to be\n"
        "            the resource at SRC_PATH.\n"
        "\n"
        "    dhub set default PATH VALUE\n"
        "            Sets the default value of the resource at PATH to be VALUE.\n"
        "\n"
        "    dhub set override PATH VALUE\n"
        "            Overrides the resource at PATH to the value VALUE.\n"
        "\n"
        "    dhub set minPeriod PATH VALUE\n"
        "            Sets the minimum time (seconds) that an Observation will wait\n"
        "            after it receives a sample before it will accept another one.\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub set lowLimit PATH VALUE\n"
        "            Sets the numeric filter lower value limit for an Observation.\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub set highLimit PATH VALUE\n"
        "            Sets the numeric filter higher value limit for an Observation.\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub set changeBy PATH VALUE\n"
        "            Sets the numeric filter hysteresis magnitude for an Observation.\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub set bufferSize PATH VALUE\n"
        "            Sets the maximum number of samples that an Observation will buffer.\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub set backupPeriod PATH VALUE\n"
        "            Sets the minimum time (seconds) that an Observation will wait\n"
        "            after performing a non-volatile backup of its buffer before it\n"
        "            performs another backup. 0 = disable non-volatile backups.\n"
        "\n"
        "            ** WARNING ** - Beware of flash memory wear!\n"
        "\n"
        "            PATH is expected to be under /obs/.  Setting this will create\n"
        "            an Observation resource at PATH if one does not already exist\n"
        "            there.\n"
        "\n"
        "    dhub remove OBJECT PATH\n"
        "            Removes an OBJECT associated with the resource at PATH.\n"
        "            Valid values for OBJECT are the same as for 'dhub get', with\n"
        "            the notable addition of 'obs', which is used to delete an\n"
        "            entire Observation resource, including all the settings\n"
        "            attached to it.\n"
        "\n"
        "    dhub get OBJECT PATH\n"
        "            Prints the state of an OBJECT associated with the resource at PATH.\n"
        "            Valid values for OBJECT are:\n"
        "              source\n"
        "              default\n"
        "              override\n"
        "              minPeriod\n"
        "              lowLimit\n"
        "              highLimit\n"
        "              changeBy\n"
        "            For the source, default, and override objects, the PATH must be\n"
        "            absolute (beginning with '/'). The other objects are only found\n"
        "            on Observations, so their PATH can be relative to /obs/.\n"
        "\n"
        "    dhub help\n"
        "    dhub -h\n"
        "    dhub --help\n"
        "           Print this help text and exit.\n"
        "\n"
        "    All output is always sent to stdout and error messages to stderr.\n"
        "\n"
        );

        exit(EXIT_SUCCESS);
}


//--------------------------------------------------------------------------------------------------
/**
 * Ptr to the PATH command-line argument, or "/" by default.
 */
//--------------------------------------------------------------------------------------------------
static const char* PathArg = "/";


//--------------------------------------------------------------------------------------------------
/**
 * Ptr to the SRC_PATH command-line argument, or NULL if there wasn't one provided.
 */
//--------------------------------------------------------------------------------------------------
static const char* SrcPathArg = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Ptr to the VALUE command-line argument, or NULL if there wasn't one provided.
 */
//--------------------------------------------------------------------------------------------------
static const char* ValueArg = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Handles a failure to connect an IPC session with the Data Hub by reporting an error to stderr
 * and exiting with EXIT_FAILURE.
 */
//--------------------------------------------------------------------------------------------------
static void HandleConnectionError
(
    const char* serviceName,    ///< The name of the service to which we failed to connect.
    le_result_t errorCode   ///< Error result code returned by the TryConnectService() function.
)
//--------------------------------------------------------------------------------------------------
{
    fprintf(stderr, "***ERROR: Can't connect to the Data Hub.\n");

    switch (errorCode)
    {
        case LE_UNAVAILABLE:
            fprintf(stderr, "%s service not currently available.\n", serviceName);
            break;

        case LE_NOT_PERMITTED:
            fprintf(stderr,
                    "Missing binding to %s service.\n"
                    "System misconfiguration detected.\n",
                    serviceName);
            break;

        case LE_COMM_ERROR:
            fprintf(stderr,
                    "Service Directory is unreachable.\n"
                    "Perhaps the Service Directory is not running?\n");
            break;

        default:
            printf("Unexpected result code %d (%s)\n", errorCode, LE_RESULT_TXT(errorCode));
            break;
    }

    exit(EXIT_FAILURE);
}


//--------------------------------------------------------------------------------------------------
/**
 * Opens IPC sessions with the Data Hub.
 */
//--------------------------------------------------------------------------------------------------
static void ConnectToDataHub
(
    void
)
//--------------------------------------------------------------------------------------------------
{
    le_result_t result = admin_TryConnectService();
    if (result != LE_OK)
    {
        HandleConnectionError("Data Hub Admin", result);
    }

    result = query_TryConnectService();
    if (result != LE_OK)
    {
        HandleConnectionError("Data Hub Query", result);
    }

    result = io_TryConnectService();
    if (result != LE_OK)
    {
        HandleConnectionError("Data Hub I/O", result);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a pointer to the entry name part of a given path.
 */
//--------------------------------------------------------------------------------------------------
const char* GetEntryName
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    const char* namePtr = strrchr(path, '/');

    if (namePtr == NULL)
    {
        return path;
    }

    if (namePtr[1] != '\0')
    {
        namePtr++;
    }

    return namePtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string describing an entry type.
 *
 * @return The entry type name.
 */
//--------------------------------------------------------------------------------------------------
static const char* EntryTypeStr
(
    admin_EntryType_t entryType
)
//--------------------------------------------------------------------------------------------------
{
    switch (entryType)
    {
        case ADMIN_ENTRY_TYPE_NONE:         return "** error: does not exist **";
        case ADMIN_ENTRY_TYPE_NAMESPACE:    return "namespace";
        case ADMIN_ENTRY_TYPE_INPUT:        return "input";
        case ADMIN_ENTRY_TYPE_OUTPUT:       return "output";
        case ADMIN_ENTRY_TYPE_OBSERVATION:  return "observation";
        case ADMIN_ENTRY_TYPE_PLACEHOLDER:  return "placeholder";
    }

    return "** error: unrecognized entry type **";
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string describing an data type.
 *
 * @return The data type name.
 */
//--------------------------------------------------------------------------------------------------
static const char* DataTypeStr
(
    io_DataType_t dataType
)
//--------------------------------------------------------------------------------------------------
{
    switch (dataType)
    {
        case IO_DATA_TYPE_TRIGGER:      return "trigger";
        case IO_DATA_TYPE_BOOLEAN:      return "Boolean";
        case IO_DATA_TYPE_NUMERIC:      return "numeric";
        case IO_DATA_TYPE_STRING:       return "string";
        case IO_DATA_TYPE_JSON:         return "JSON";
    }

    return "** error: unrecognized entry type **";
}

//--------------------------------------------------------------------------------------------------
/**
 * Print an indent to stdout.
 */
//--------------------------------------------------------------------------------------------------
static void Indent
(
    size_t depth
)
//--------------------------------------------------------------------------------------------------
{
    for (size_t i = 0; i < depth; i++)
    {
        printf("   ");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the current value of a given resource.
 */
//--------------------------------------------------------------------------------------------------
static void PrintCurrentValue
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    double timestamp;
    char value[IO_MAX_STRING_VALUE_LEN + 1];

    le_result_t result = query_GetJson(path, &timestamp, value, sizeof(value));

    if (result == LE_OK)
    {
        printf("%s (ts: %lf)\n", value, timestamp);
    }
    else if (result == LE_UNAVAILABLE)
    {
        if (admin_IsMandatory(path))
        {
            printf(" <-- WARNING: unsatisfied mandatory output\n");
        }
        else
        {
            putchar('\n');
        }
    }
    else
    {
        fprintf(stderr, "** ERROR: %s\n", LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the default value associated with a given resource.
 */
//--------------------------------------------------------------------------------------------------
static void PrintDefault
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    le_result_t result;

    io_DataType_t dataType = admin_GetDefaultDataType(path);

    switch (dataType)
    {
        case IO_DATA_TYPE_TRIGGER:

            LE_FATAL("...a trigger?!\n"); // This should never happen.

        case IO_DATA_TYPE_BOOLEAN:

            if (admin_GetBooleanDefault(path))
            {
                printf("true");
            }
            else
            {
                printf("false");
            }
            break;

        case IO_DATA_TYPE_NUMERIC:

            printf("%lf", admin_GetNumericDefault(path));
            break;

        case IO_DATA_TYPE_STRING:
        {
            char string[IO_MAX_STRING_VALUE_LEN + 1];

            result = admin_GetStringDefault(path, string, sizeof(string));
            LE_ASSERT(result != LE_OVERFLOW);
            if (result == LE_NOT_FOUND)
            {
                printf("unable to retrieve string value.");
            }
            else
            {
                printf("\"%s\"", string);
            }
            break;
        }

        case IO_DATA_TYPE_JSON:
        {
            char string[IO_MAX_STRING_VALUE_LEN + 1];

            result = admin_GetJsonDefault(path, string, sizeof(string));
            LE_ASSERT(result != LE_OVERFLOW);
            if (result == LE_NOT_FOUND)
            {
                printf("unable to retrieve JSON value.");
            }
            else
            {
                printf("JSON: %s", string);
            }
            break;
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print out a numeric setting that is of type double (where NAN = "not set").
 */
//--------------------------------------------------------------------------------------------------
static void PrintDoubleSetting
(
    const char* label,
    double value
)
//--------------------------------------------------------------------------------------------------
{
    if (value == NAN)
    {
        printf("%s: not set\n", label);
    }
    else
    {
        printf("%s: %lf\n", label, value);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the data type of a resource at a given path.
 */
//--------------------------------------------------------------------------------------------------
static void PrintDataType
(
    const char* path,
    size_t depth
)
//--------------------------------------------------------------------------------------------------
{
    io_DataType_t dataType;

    le_result_t result = query_GetDataType(path, &dataType);

    if (result == LE_OK)
    {
        Indent(depth);
        printf("data type = %s\n", DataTypeStr(dataType));
    }
    else
    {
        fprintf(stderr, "** Error getting data type: %d (%s).\n", result, LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the units of a resource at a given path (if any specified).
 */
//--------------------------------------------------------------------------------------------------
static void PrintUnits
(
    const char* path,
    size_t depth
)
//--------------------------------------------------------------------------------------------------
{
    char units[IO_MAX_UNITS_NAME_LEN + 1];

    le_result_t result = query_GetUnits(path, units, sizeof(units));

    if (result == LE_OK)
    {
        if (units[0] != '\0')
        {
            Indent(depth);
            printf("units = '%s'\n", units);
        }
    }
    else
    {
        fprintf(stderr, "** Error getting units: %d (%s).\n", result, LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the details of an entry in the resource tree at a given path.
 */
//--------------------------------------------------------------------------------------------------
static void PrintEntry
(
    const char* path,
    size_t depth    ///< Indentation depth
)
//--------------------------------------------------------------------------------------------------
{
    le_result_t result;

    const char* name = GetEntryName(path);

    admin_EntryType_t entryType = admin_GetEntryType(path);

    if (entryType == ADMIN_ENTRY_TYPE_NAMESPACE)
    {
        // There's not much to print for a Namespace.
        Indent(depth);
        printf("%s\n", name);
    }
    else // Resource
    {
        Indent(depth);
        printf("%s <%s> = ", name, EntryTypeStr(entryType));
        PrintCurrentValue(path);

        depth += 2;

        PrintDataType(path, depth);

        PrintUnits(path, depth);

        if (admin_IsOverridden(path))
        {
            Indent(depth);
            printf("** overridden **\n");
        }

        if (admin_HasDefault(path))
        {
            Indent(depth);
            printf("default = ");
            PrintDefault(path);
            putchar('\n');
        }

        char srcPath[IO_MAX_RESOURCE_PATH_LEN + 1];
        result = admin_GetSource(path, srcPath, sizeof(srcPath));
        if (result == LE_OK)
        {
            Indent(depth);
            printf("receiving data from '%s'", srcPath);

            if (entryType == ADMIN_ENTRY_TYPE_INPUT)
            {
                Indent(depth);
                printf(" (which will be ignored because this is an input)");
            }

            putchar('\n');
        }
        else if (result != LE_NOT_FOUND)
        {
            LE_FATAL("Bug: Unexpected result from admin_GetSource(): %d (%s).",
                     result,
                     LE_RESULT_TXT(result));
        }

    }

    // Observation
    if (entryType == ADMIN_ENTRY_TYPE_OBSERVATION)
    {
        const char* obsPath = path + 5;  // Skip "/obs/" to convert to relative path.

        Indent(depth);
        PrintDoubleSetting("minPeriod", admin_GetMinPeriod(obsPath));
        Indent(depth);
        PrintDoubleSetting("lowLimit", admin_GetLowLimit(obsPath));
        Indent(depth);
        PrintDoubleSetting("highLimit", admin_GetHighLimit(obsPath));
        Indent(depth);
        PrintDoubleSetting("changeBy", admin_GetChangeBy(obsPath));
        Indent(depth);
        printf("bufferSize: %u entries\n", admin_GetBufferMaxCount(obsPath));
        Indent(depth);
        uint32_t backupPeriod = admin_GetBufferBackupPeriod(obsPath);
        printf("backupPeriod: %u seconds (= %lf minutes) (= %lf hours)\n",
               backupPeriod,
               ((double)backupPeriod) / 60,
               ((double)backupPeriod) / 3600);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the details of a branch of the resource tree, starting from a given path.
 */
//--------------------------------------------------------------------------------------------------
static void PrintBranch
(
    const char* path,
    size_t depth    ///< Indentation depth
)
//--------------------------------------------------------------------------------------------------
{
    PrintEntry(path, depth);

    char childPath[IO_MAX_RESOURCE_PATH_LEN + 1];

    le_result_t result = admin_GetFirstChild(path, childPath, sizeof(childPath));
    LE_ASSERT(result != LE_OVERFLOW);

    while (result == LE_OK)
    {
        PrintBranch(childPath, depth + 1);

        result = admin_GetNextSibling(childPath, childPath, sizeof(childPath));

        LE_ASSERT(result != LE_OVERFLOW);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the data flow route into destPath to be from srcPath.
 */
//--------------------------------------------------------------------------------------------------
static void SetSource
(
    const char* destPath,
    const char* srcPath
)
//--------------------------------------------------------------------------------------------------
{
    le_result_t result = admin_SetSource(destPath, srcPath);

    switch (result)
    {
        case LE_OK:

            printf("Added route '%s' -> '%s'.\n", srcPath, destPath);
            break;

        case LE_BAD_PARAMETER:

            fprintf(stderr, "One or both of the resource paths are malformed.\n");
            exit(EXIT_FAILURE);

        case LE_DUPLICATE:

            fprintf(stderr,
                    "Addition of a route from '%s' to '%s' would create a loop.\n",
                    srcPath,
                    destPath);
            exit(EXIT_FAILURE);

        default:

            fprintf(stderr,
                    "Unexpected result code %d (%s) from Data Hub.\n",
                    result,
                    LE_RESULT_TXT(result));
            exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the current data flow route source for a given resource.
 */
//--------------------------------------------------------------------------------------------------
static void PrintSource
(
    const char* destPath
)
//--------------------------------------------------------------------------------------------------
{
    char srcPath[IO_MAX_RESOURCE_PATH_LEN + 1];

    le_result_t result = admin_GetSource(destPath, srcPath, sizeof(srcPath));

    if (result == LE_OK)
    {
        puts(srcPath);

        admin_EntryType_t entryType = admin_GetEntryType(destPath);

        if (entryType == ADMIN_ENTRY_TYPE_INPUT)
        {
            printf("WARNING: Input '%s' will ignore data pushed to it by '%s'.\n",
                   destPath,
                   srcPath);
            printf("Input resources only accept data pushed by the app that created them.\n");
        }
    }
    else if (result != LE_NOT_FOUND)
    {
        LE_FATAL("Bug: Unexpected result from admin_GetSource(): %d (%s).",
                 result,
                 LE_RESULT_TXT(result));
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Print the default value of a given resource.
 */
//--------------------------------------------------------------------------------------------------
static void GetDefault
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    if (admin_HasDefault(PathArg))
    {
        PrintDefault(PathArg);
        putchar('\n');
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Attempts to parse a string as a double-precision floating-point number.
 *
 * @return The value or NAN if unsuccessful. Sets errno to ERANGE or EINVAL on error.
 *
 * @note Clears errno to 0 on success.
 */
//--------------------------------------------------------------------------------------------------
static double ParseDouble
(
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    char* endPtr;

    errno = 0;

    double number = strtod(value, &endPtr);

    if (errno != 0)
    {
        return NAN;
    }

    if (*endPtr != '\0')
    {
        errno = EINVAL;

        return NAN;
    }

    return number;
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a setting.
 */
//--------------------------------------------------------------------------------------------------
static void SetSetting
(
    const char* path,
    const char* value,
    void (*booleanFunc)(const char*, bool),
    void (*numericFunc)(const char*, double),
    void (*stringFunc)(const char*, const char*),
    void (*jsonFunc)(const char*, const char*)
)
//--------------------------------------------------------------------------------------------------
{
    if (strcmp("true", value) == 0)
    {
        booleanFunc(path, true);
    }
    else if (strcmp("false", value) == 0)
    {
        booleanFunc(path, false);
    }
    else
    {
        // Try parsing as a number.
        double number = ParseDouble(value);
        if (errno == 0)
        {
            numericFunc(path, number);
        }
        // If that didn't work, treat as a string.
        else
        {
            stringFunc(path, value);
        }

        // TODO: Add a --json (-j) option to specify JSON.
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set a floating-point numeric setting on an Observation.
 *
 * @note Has the side-effect of creating the Observation if it does not yet exist.
 */
//--------------------------------------------------------------------------------------------------
static void SetDoubleSetting
(
    const char* path,
    const char* valueStr,
    void (*func)(const char*, double)
)
//--------------------------------------------------------------------------------------------------
{
    double number = ParseDouble(valueStr);
    if (errno == 0)
    {
        if (admin_CreateObs(path) != LE_OK)
        {
            fprintf(stderr, "Invalid resource path for Observation.");
            exit(EXIT_FAILURE);
        }

        func(path, number);
    }
    else
    {
        fprintf(stderr, "Value must be numeric ('%s' is not).\n", valueStr);
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a floating-point numeric setting.
 */
//--------------------------------------------------------------------------------------------------
static void GetDoubleSetting
(
    const char* path,
    double (*getterFunc)(const char*)
)
//--------------------------------------------------------------------------------------------------
{
    double value = getterFunc(path);

    if (value != NAN)
    {
        printf("%lf\n", value);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set an integer setting.
 *
 * @note Has the side-effect of creating the Observation if it does not yet exist.
 */
//--------------------------------------------------------------------------------------------------
static void SetIntegerSetting
(
    const char* path,
    const char* valueStr,
    void (*setterFunc)(const char*, uint32_t)
)
//--------------------------------------------------------------------------------------------------
{
    int value;
    if ((le_utf8_ParseInt(&value, valueStr) != LE_OK) || (value < 0))
    {
        fprintf(stderr, "Non-negative integer value required.");
        exit(EXIT_FAILURE);
    }

    if (admin_CreateObs(path) != LE_OK)
    {
        fprintf(stderr, "Invalid resource path for Observation.");
        exit(EXIT_FAILURE);
    }

    setterFunc(path, (uint32_t)value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get an integer setting.
 */
//--------------------------------------------------------------------------------------------------
static void GetIntegerSetting
(
    const char* path,
    uint32_t (*getterFunc)(const char*)
)
//--------------------------------------------------------------------------------------------------
{
    uint32_t value = getterFunc(path);

    printf("%u\n", value);
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the default value of a resource.
 */
//--------------------------------------------------------------------------------------------------
static void SetDefault
(
    const char* path,
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    SetSetting(path,
               value,
               admin_SetBooleanDefault,
               admin_SetNumericDefault,
               admin_SetStringDefault,
               admin_SetJsonDefault);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the override for a resource.
 */
//--------------------------------------------------------------------------------------------------
static void GetOverride
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    if (admin_IsOverridden(path))
    {
        printf("It's overridden, but can't tell to what until the Query API is implemented.\n");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Set the override for a resource.
 */
//--------------------------------------------------------------------------------------------------
static void SetOverride
(
    const char* path,
    const char* value
)
//--------------------------------------------------------------------------------------------------
{
    SetSetting(path,
               value,
               admin_SetBooleanOverride,
               admin_SetNumericOverride,
               admin_SetStringOverride,
               admin_SetJsonOverride);
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform validity check on an absolute resource path.
 *
 * @return Pointer to the validated path.
 */
//--------------------------------------------------------------------------------------------------
static const char* ValidateAbsolutePath
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    if (path[0] != '/')
    {
        fprintf(stderr, "Resource paths must be absolute (i.e., must begin with '/').\n");
        exit(EXIT_FAILURE);
    }

    return path;
}


//--------------------------------------------------------------------------------------------------
/**
 * Perform validity check on an Observation path.
 *
 * @return Pointer to the validated path.
 */
//--------------------------------------------------------------------------------------------------
static const char* ValidateObservationPath
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    if (path[0] == '/')
    {
        if (strncmp(path, "/obs/", 5) == 0)
        {
            return path;
        }

        fprintf(stderr, "Observation paths must be relative (not beginning with '/');\n"
                        "unless they begin with '/obs/'.\n");
        exit(EXIT_FAILURE);
    }

    return path;
}


//--------------------------------------------------------------------------------------------------
/**
 * Command-line argument handler call-back for a SRC_PATH argument to 'source' command.
 */
//--------------------------------------------------------------------------------------------------
static void SrcPathArgHandler
(
    const char* arg
)
//--------------------------------------------------------------------------------------------------
{
    SrcPathArg = ValidateAbsolutePath(arg);
}


//--------------------------------------------------------------------------------------------------
/**
 * Command-line argument handler call-back for a value argument.
 */
//--------------------------------------------------------------------------------------------------
static void ValueArgHandler
(
    const char* arg
)
//--------------------------------------------------------------------------------------------------
{
    // It may be hard to tell what type to parse the value argument as until we have finished
    // parsing the command line, so we don't do anything here, other than record the pointer
    // to the value string for later.
    ValueArg = arg;
}


//--------------------------------------------------------------------------------------------------
/**
 * Command-line argument handler call-back for a PATH argument.
 */
//--------------------------------------------------------------------------------------------------
static void PathArgHandler
(
    const char* arg
)
//--------------------------------------------------------------------------------------------------
{
    switch (Object)
    {
        case OBJECT_SOURCE:
        case OBJECT_DEFAULT:
        case OBJECT_OVERRIDE:

            PathArg = ValidateAbsolutePath(arg);
            break;

        case OBJECT_MIN_PERIOD:
        case OBJECT_LOW_LIMIT:
        case OBJECT_HIGH_LIMIT:
        case OBJECT_CHANGE_BY:
        case OBJECT_BUFFER_SIZE:
        case OBJECT_BACKUP_PERIOD:
        case OBJECT_OBSERVATION:

            PathArg = ValidateObservationPath(arg);
            break;

        default:
            LE_FATAL("Object unknown.");
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Command-line argument handler callback for the object type argument (e.g., "source", "default").
 */
//--------------------------------------------------------------------------------------------------
static void ObjectTypeArgHandler
(
    const char* arg
)
//--------------------------------------------------------------------------------------------------
{
    if (strcmp(arg, "source") == 0)
    {
        Object = OBJECT_SOURCE;
    }
    else if (strcmp(arg, "default") == 0)
    {
        Object = OBJECT_DEFAULT;
    }
    else if (strcmp(arg, "override") == 0)
    {
        Object = OBJECT_OVERRIDE;
    }
    else if (strcmp(arg, "minPeriod") == 0)
    {
        Object = OBJECT_MIN_PERIOD;
    }
    else if (strcmp(arg, "lowLimit") == 0)
    {
        Object = OBJECT_LOW_LIMIT;
    }
    else if (strcmp(arg, "highLimit") == 0)
    {
        Object = OBJECT_HIGH_LIMIT;
    }
    else if (strcmp(arg, "changeBy") == 0)
    {
        Object = OBJECT_CHANGE_BY;
    }
    else if (strcmp(arg, "bufferSize") == 0)
    {
        Object = OBJECT_BUFFER_SIZE;
    }
    else if (strcmp(arg, "backupPeriod") == 0)
    {
        Object = OBJECT_BACKUP_PERIOD;
    }
    else if ((strcmp(arg, "obs") == 0) || (strcmp(arg, "observation") == 0))
    {
        Object = OBJECT_OBSERVATION;
    }
    else
    {
        fprintf(stderr, "Unknown object type '%s'.\n", arg);
        exit(EXIT_FAILURE);
    }

    // Expect a mandatory PATH argument.
    le_arg_AddPositionalCallback(PathArgHandler);

    // If the action is "set", then also expect another argument after that.
    if (Action == ACTION_SET)
    {
        if (Object == OBJECT_SOURCE)
        {
            // The "set source" command needs the SRC_PATH.
            le_arg_AddPositionalCallback(SrcPathArgHandler);
        }
        else if (Object == OBJECT_OBSERVATION)
        {
            fprintf(stderr, "Can't 'set' an Observation.\n");
            exit(EXIT_FAILURE);
        }
        else
        {
            // Everything else needs a VALUE.
            le_arg_AddPositionalCallback(ValueArgHandler);
        }
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Command-line argument handler call-back for the first positional argument, which is the command.
 */
//--------------------------------------------------------------------------------------------------
static void CommandArgHandler
(
    const char* arg
)
//--------------------------------------------------------------------------------------------------
{
    if (strcmp(arg, "help") == 0)
    {
        Action = ACTION_HELP;
    }
    else if (strcmp(arg, "list") == 0)
    {
        Action = ACTION_LIST;

        // Accept an optional PATH argument.
        le_arg_AddPositionalCallback(PathArgHandler);
        le_arg_AllowLessPositionalArgsThanCallbacks();
    }
    else if (strcmp(arg, "get") == 0)
    {
        Action = ACTION_GET;

        // Expect an object type argument ("source", "default" or "override").
        le_arg_AddPositionalCallback(ObjectTypeArgHandler);
    }
    else if (strcmp(arg, "set") == 0)
    {
        Action = ACTION_SET;

        // Expect an object type argument ("source", "default" or "override").
        le_arg_AddPositionalCallback(ObjectTypeArgHandler);
    }
    else if (strcmp(arg, "remove") == 0)
    {
        Action = ACTION_REMOVE;

        // Expect an object type argument ("source", "default" or "override").
        le_arg_AddPositionalCallback(ObjectTypeArgHandler);
    }
    else
    {
        fprintf(stderr, "Unrecognized command '%s'.  Try 'dhub help' for assistance.\n", arg);
        exit(EXIT_FAILURE);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Component initializer.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    le_arg_SetFlagCallback(HandleHelpRequest, "h", "help");

    le_arg_AddPositionalCallback(CommandArgHandler);

    le_arg_Scan();

    ConnectToDataHub();

    switch (Action)
    {
        case ACTION_HELP:

            HandleHelpRequest();
            break;

        case ACTION_LIST:

            PrintBranch(PathArg, 0);
            break;

        case ACTION_GET:

            switch (Object)
            {
                case OBJECT_SOURCE:

                    PrintSource(PathArg);
                    break;

                case OBJECT_DEFAULT:

                    GetDefault(PathArg);
                    break;

                case OBJECT_OVERRIDE:

                    GetOverride(PathArg);
                    break;

                case OBJECT_MIN_PERIOD:

                    GetDoubleSetting(PathArg, admin_GetMinPeriod);
                    break;

                case OBJECT_LOW_LIMIT:

                    GetDoubleSetting(PathArg, admin_GetLowLimit);
                    break;

                case OBJECT_HIGH_LIMIT:

                    GetDoubleSetting(PathArg, admin_GetHighLimit);
                    break;

                case OBJECT_CHANGE_BY:

                    GetDoubleSetting(PathArg, admin_GetChangeBy);
                    break;

                case OBJECT_BUFFER_SIZE:

                    GetIntegerSetting(PathArg, admin_GetBufferMaxCount);
                    break;

                case OBJECT_BACKUP_PERIOD:

                    GetIntegerSetting(PathArg, admin_GetBufferBackupPeriod);
                    break;

                case OBJECT_OBSERVATION:

                    fprintf(stderr, "Can't 'get' an Observation.\n");
                    exit(EXIT_FAILURE);
            }
            break;

        case ACTION_SET:

            switch (Object)
            {
                case OBJECT_SOURCE:

                    SetSource(PathArg, SrcPathArg);
                    break;

                case OBJECT_DEFAULT:

                    SetDefault(PathArg, ValueArg);
                    break;

                case OBJECT_OVERRIDE:

                    SetOverride(PathArg, ValueArg);
                    break;

                case OBJECT_MIN_PERIOD:

                    SetDoubleSetting(PathArg, ValueArg, admin_SetMinPeriod);
                    break;

                case OBJECT_LOW_LIMIT:

                    SetDoubleSetting(PathArg, ValueArg, admin_SetLowLimit);
                    break;

                case OBJECT_HIGH_LIMIT:

                    SetDoubleSetting(PathArg, ValueArg, admin_SetHighLimit);
                    break;

                case OBJECT_CHANGE_BY:

                    SetDoubleSetting(PathArg, ValueArg, admin_SetChangeBy);
                    break;

                case OBJECT_BUFFER_SIZE:

                    SetIntegerSetting(PathArg, ValueArg, admin_SetBufferMaxCount);
                    break;

                case OBJECT_BACKUP_PERIOD:

                    SetIntegerSetting(PathArg, ValueArg, admin_SetBufferBackupPeriod);
                    break;

                case OBJECT_OBSERVATION:

                    fprintf(stderr, "Can't 'set' an Observation.\n");
                    exit(EXIT_FAILURE);
            }
            break;

        case ACTION_REMOVE:

            switch (Object)
            {
                case OBJECT_SOURCE:

                    admin_RemoveSource(PathArg);
                    break;

                case OBJECT_DEFAULT:

                    admin_RemoveDefault(PathArg);
                    break;

                case OBJECT_OVERRIDE:

                    admin_RemoveOverride(PathArg);
                    break;

                case OBJECT_MIN_PERIOD:

                    admin_SetMinPeriod(PathArg, NAN);
                    break;

                case OBJECT_LOW_LIMIT:

                    admin_SetLowLimit(PathArg, NAN);
                    break;

                case OBJECT_HIGH_LIMIT:

                    admin_SetHighLimit(PathArg, NAN);
                    break;

                case OBJECT_CHANGE_BY:

                    admin_SetChangeBy(PathArg, NAN);
                    break;

                case OBJECT_BUFFER_SIZE:
                case OBJECT_BACKUP_PERIOD:

                    fprintf(stderr, "These cannot be removed. Do you mean to set them to zero?\n");
                    exit(EXIT_FAILURE);

                case OBJECT_OBSERVATION:

                    admin_DeleteObs(PathArg);
                    break;
            }
            break;

        default:

            LE_FATAL("Unimplemented action.");
            break;
    }

    exit(EXIT_SUCCESS);
}
