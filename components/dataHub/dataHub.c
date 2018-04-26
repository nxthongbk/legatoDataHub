//--------------------------------------------------------------------------------------------------
/**
 * @file dataHub.c
 *
 * Data Hub component initializer and utilities shared by other modules.
 *
 * The Resource Tree structure and Namespaces are implemented by the resTree module.
 *
 * The Resource base class and Placeholder resource are implemented by the resource module
 * (prefix = res_).
 *
 * Inputs and Outputs are implemented by the ioRes module.
 *
 * Observations are implemented by the obs module.
 *
 * Data Samples are implemented by the dataSample module.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "dataHub.h"
#include "nan.h"
#include "dataSample.h"
#include "handler.h"
#include "resource.h"
#include "resTree.h"
#include "ioPoint.h"
#include "obs.h"
#include "ioService.h"


//--------------------------------------------------------------------------------------------------
/**
 * Component initializer.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    dataSample_Init();
    handler_Init();
    res_Init();
    ioPoint_Init();
    obs_Init();
    resTree_Init();
    ioService_Init();

    LE_INFO("Data Hub started.");
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given data type (e.g., "numeric").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetDataTypeName
(
    io_DataType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case IO_DATA_TYPE_TRIGGER:
            return "trigger";

        case IO_DATA_TYPE_BOOLEAN:
            return "Boolean";

        case IO_DATA_TYPE_NUMERIC:
            return "numeric";

        case IO_DATA_TYPE_STRING:
            return "string";

        case IO_DATA_TYPE_JSON:
            return "JSON";
    }

    LE_FATAL("Unknown data type %d.", type);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given resource tree entry type (e.g., "observation").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetEntryTypeName
(
    admin_EntryType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case ADMIN_ENTRY_TYPE_NONE:
            return "** none **";

        case ADMIN_ENTRY_TYPE_NAMESPACE:
            return "namespace";

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:
            return "placeholder";

        case ADMIN_ENTRY_TYPE_INPUT:
            return "input";

        case ADMIN_ENTRY_TYPE_OUTPUT:
            return "output";

        case ADMIN_ENTRY_TYPE_OBSERVATION:
            return "observation";
    }

    LE_FATAL("Unknown entry type %d.", type);
}
