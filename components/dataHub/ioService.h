//--------------------------------------------------------------------------------------------------
/**
 * @file ioService.h
 *
 * Declarations of functions that are provided by the ioService module to other modules inside
 * the Data Hub.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef IO_SERVICE_H_INCLUDE_GUARD
#define IO_SERVICE_H_INCLUDE_GUARD


//--------------------------------------------------------------------------------------------------
/**
 * Initializes the module.  Must be called before any other functions in the module are called.
 */
//--------------------------------------------------------------------------------------------------
void ioService_Init
(
    void
);

//--------------------------------------------------------------------------------------------------
/**
 * Notify apps that care that administrative changes are about to be performed.
 *
 * This will result in call-backs to any handlers registered using io_AddUpdateStartEndHandler().
 */
//--------------------------------------------------------------------------------------------------
void ioService_StartUpdate
(
    void
);

//--------------------------------------------------------------------------------------------------
/**
 * Notify apps that care that all pending administrative changes have been applied and that
 * normal operation may resume.
 *
 * This will result in call-backs to any handlers registered using io_AddUpdateStartEndHandler().
 */
//--------------------------------------------------------------------------------------------------
void ioService_EndUpdate
(
    void
);


#endif // IO_SERVICE_H_INCLUDE_GUARD
