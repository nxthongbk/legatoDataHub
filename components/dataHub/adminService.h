//--------------------------------------------------------------------------------------------------
/**
 * @file adminService.h
 *
 * Declarations of functions that are provided by the adminService module to other modules inside
 * the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef ADMIN_SERVICE_H_INCLUDE_GUARD
#define ADMIN_SERVICE_H_INCLUDE_GUARD

//--------------------------------------------------------------------------------------------------
/**
 * Initializes the module.  Must be called before any other functions in the module are called.
 */
//--------------------------------------------------------------------------------------------------
void adminService_Init
(
    void
);

//--------------------------------------------------------------------------------------------------
/**
 * Call all the registered Resource Tree Change Handlers.
 */
//--------------------------------------------------------------------------------------------------
void admin_CallResourceTreeChangeHandlers
(
    const char* path,
    admin_EntryType_t entryType,
    admin_ResourceOperationType_t resourceOperationType
);

#endif // ADMIN_SERVICE_H_INCLUDE_GUARD
