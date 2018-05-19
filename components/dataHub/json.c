//--------------------------------------------------------------------------------------------------
/**
 * @file json.c
 *
 * Implementation of JSON parsing functionality.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "dataHub.h"
#include "nan.h"
#include "dataSample.h"
#include "json.h"


//--------------------------------------------------------------------------------------------------
/**
 * Skip over whitespace, if any.
 *
 * @return Pointer to the first non-whitespace character in the string.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipWhitespace
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (valPtr != NULL)
    {
        while (isspace(*valPtr))
        {
            valPtr++;
        }
    }

    return valPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip over a string.
 *
 * @return Pointer to the first non-whitespace character after the string, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipString
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '"')
    {
        return NULL;
    }

    valPtr++;

    while (*valPtr != '"')
    {
        if (*valPtr == '\0')
        {
            return NULL;
        }

        // Skip an escaped quote.
        if ((valPtr[0] == '\\') && (valPtr[1] == '"'))
        {
            valPtr += 2;
        }
        else
        {
            valPtr += 1;
        }
    }

    return SkipWhitespace(valPtr + 1);
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip a literal value, such as (e.g., true, false, or null).
 *
 * @return Pointer to the first non-whitespace character after the value, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipLiteral
(
    const char* valPtr,
    const char* textToSkip,
    size_t lenToSkip
)
//--------------------------------------------------------------------------------------------------
{
    if (strncmp(valPtr, textToSkip, lenToSkip) == 0)
    {
        valPtr += lenToSkip;

        // The literal must be followed by space, a comma, a closing bracket or end of the string.
        if (isspace(*valPtr))
        {
            return SkipWhitespace(valPtr + 1);
        }
        else if (   (*valPtr == ',')
                 || (*valPtr == ']')
                 || (*valPtr == '}')
                 || (*valPtr == '\0')  )
        {
            return valPtr;
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip a number.
 *
 * @return Pointer to the first non-whitespace character after the number, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipNumber
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    char* endPtr = NULL;

    (void)strtod(valPtr, &endPtr);

    if (endPtr != valPtr)
    {
        // The number must be followed by space, a comma, a closing bracket or end of the string.
        if (isspace(*endPtr))
        {
            return SkipWhitespace(endPtr + 1);
        }
        else if (   (*endPtr == ',')
                 || (*endPtr == ']')
                 || (*endPtr == '}')
                 || (*endPtr == '\0')  )
        {
            return endPtr;
        }
    }

    return NULL;
}


static const char* SkipObject(const char* valPtr);
static const char* SkipArray(const char* valPtr);


//--------------------------------------------------------------------------------------------------
/**
 * Skip any kind of JSON value.
 *
 * @return Pointer to the first non-whitespace character after the value, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipValue
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    switch (*valPtr)
    {
        case '{':

            return SkipObject(valPtr);

        case '[':

            return SkipArray(valPtr);

        case '"':

            return SkipString(valPtr);

        case 't':

            return SkipLiteral(valPtr, "true", 4);

        case 'f':

            return SkipLiteral(valPtr, "false", 5);

        case 'n':

            return SkipLiteral(valPtr, "null", 4);

        default:

            return SkipNumber(valPtr);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an object member.
 *
 * @return Pointer to the first non-whitespace character after the member, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipMember
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    valPtr = SkipString(valPtr);

    if (valPtr != NULL)
    {
        if (*valPtr != ':')
        {
            return NULL;
        }

        valPtr = SkipValue(SkipWhitespace(valPtr + 1));
    }

    return valPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an object.
 *
 * @return Pointer to the first non-whitespace character after the object, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipObject
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '{')
    {
        return NULL;
    }

    valPtr = SkipWhitespace(valPtr + 1);

    if (*valPtr == '}')
    {
        return SkipWhitespace(valPtr + 1);
    }

    while (*valPtr != '\0')
    {
        valPtr = SkipMember(valPtr);

        if (valPtr == NULL)
        {
            return NULL;
        }

        if (*valPtr == '}')
        {
            return SkipWhitespace(valPtr + 1);
        }

        if (*valPtr == ',')
        {
            valPtr = SkipWhitespace(valPtr + 1);
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Skip an array.
 *
 * @return Pointer to the first non-whitespace character after the array, or NULL on error.
 */
//--------------------------------------------------------------------------------------------------
static const char* SkipArray
(
    const char* valPtr
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '[')
    {
        return NULL;
    }

    valPtr = SkipWhitespace(valPtr + 1);

    if (*valPtr == ']')
    {
        return SkipWhitespace(valPtr + 1);
    }

    while (*valPtr != '\0')
    {
        valPtr = SkipValue(valPtr);

        if (valPtr == NULL)
        {
            return NULL;
        }

        if (*valPtr == ']')
        {
            return SkipWhitespace(valPtr + 1);
        }

        if (*valPtr == ',')
        {
            valPtr = SkipWhitespace(valPtr + 1);
        }
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the array element at a given index in the JSON array.
 *
 * @return Pointer to the first character in the element, or NULL if not found.
 */
//--------------------------------------------------------------------------------------------------
static const char* GoToElement
(
    const char* valPtr,
    long int index
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '[')
    {
        return NULL;
    }

    valPtr++;   // Skip '['
    valPtr = SkipWhitespace(valPtr);
    for (size_t i = 0; i != index; i++)
    {
        valPtr = SkipValue(valPtr);

        if ((valPtr == NULL) || (*valPtr != ','))
        {
            return NULL;
        }

        valPtr++;   // Skip ','
        valPtr = SkipWhitespace(valPtr);
    }

    if (*valPtr == '\0')
    {
        return NULL;
    }

    return valPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find the object member with a given name in the JSON object.
 *
 * @return Pointer to the first character in the member's value, or NULL if not found.
 */
//--------------------------------------------------------------------------------------------------
static const char* GoToMember
(
    const char* valPtr,
    const char* memberName
)
//--------------------------------------------------------------------------------------------------
{
    if (*valPtr != '{')
    {
        return NULL;
    }

    size_t nameLen = strlen(memberName);

    valPtr++;   // Skip '{'
    valPtr = SkipWhitespace(valPtr);
    // If we find something other than a '"' beginning a member name, then the JSON is malformed.
    while (*valPtr == '"')
    {
        // If the member name matches,
        if ((strncmp(valPtr + 1, memberName, nameLen) == 0) && (valPtr[nameLen + 1] == '"'))
        {
            valPtr = SkipWhitespace(valPtr + nameLen + 2);
            if (*valPtr != ':')
            {
                LE_ERROR("Missing colon after JSON object member name '%s'.", memberName);
                return NULL;
            }

            return SkipWhitespace(valPtr + 1);
        }

        // The member name doesn't match, so skip over this member.
        valPtr = SkipMember(valPtr);

        // Since we haven't found the member we are looking for yet, we hope to find a comma next,
        // meaning there will be more members to follow.
        if ((valPtr == NULL) || (*valPtr != ','))
        {
            return NULL;
        }

        valPtr++;   // Skip ','
        valPtr = SkipWhitespace(valPtr);
    }

    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Extract a member name from an extraction specifier string.
 *
 * @return Pointer to the location in the extraction specifier string immediately following the
 *         extracted member name, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
static const char* GetMemberName
(
    const char* specPtr,    ///< The extraction specifier.
    char* buffPtr,  ///< Ptr to buffer where the name will be copied.
    size_t buffSize ///< Size of buffer pointed to by buffPtr.
)
//--------------------------------------------------------------------------------------------------
{
    size_t i = 0;

    while ((i < buffSize) && isalnum(*specPtr))
    {
        *buffPtr = *specPtr;

        buffPtr++;
        specPtr++;
        i++;
    }

    if (i >= buffSize)
    {
        return NULL;   // Overflow.
    }

    *buffPtr = '\0';

    return specPtr;
}


//--------------------------------------------------------------------------------------------------
/**
 * Find an object member or array element in a JSON data value, based on a given
 * extraction specifier.
 *
 * The extraction specifiers look like "x" or "x.y" or "[3]" or "x[3].y", etc.
 *
 * @return Ptr to the first character, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
static const char* Find
(
    const char* original,       ///< [IN] Original JSON string to extract from.
    const char* extractionSpec  ///< [IN] the extraction specification.
)
//--------------------------------------------------------------------------------------------------
{
    const char* specPtr = extractionSpec;
    const char* valPtr = original;

    while (valPtr && (*valPtr != '\0'))
    {
        switch (*specPtr)
        {
            case '\0':

                return valPtr;

            case '[':
            {
                char* endPtr;
                long int index = strtoul(specPtr + 1, &endPtr, 10);
                if ((index < 0) || (endPtr == (specPtr + 1)) || (*endPtr != ']'))
                {
                    goto badSpec;
                }
                specPtr = endPtr + 1;

                valPtr = GoToElement(valPtr, index);

                break;
            }

            case '.':

                specPtr++;

                // *** FALL THROUGH ***

            default:
            {
                if (!isalpha(specPtr[1]))
                {
                    goto badSpec;
                }

                char memberName[ADMIN_MAX_JSON_EXTRACTOR_LEN];
                specPtr = GetMemberName(specPtr, memberName, sizeof(memberName));
                if (specPtr == NULL)
                {
                    goto badSpec;
                }

                valPtr = GoToMember(valPtr, memberName);

                break;
            }
        }
    }

    LE_WARN("'%s' not found in JSON value '%s'.", extractionSpec, original);
    return NULL;

badSpec:

    LE_ERROR("Invalid JSON extraction spec '%s'.", extractionSpec);
    return NULL;
}


//--------------------------------------------------------------------------------------------------
/**
 * Extract an object member or array element from a JSON data value, based on a given
 * extraction specifier.
 *
 * The extraction specifiers look like "x" or "x.y" or "[3]" or "x[3].y", etc.
 *
 * @return Reference to the extracted data sample, or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
dataSample_Ref_t json_Extract
(
    dataSample_Ref_t sampleRef, ///< [IN] Original JSON data sample to extract from.
    const char* extractionSpec, ///< [IN] the extraction specification.
    io_DataType_t* dataTypePtr  ///< [OUT] Ptr to where to put the data type of the extracted object
)
//--------------------------------------------------------------------------------------------------
{
    const char* original = dataSample_GetJson(sampleRef);
    const char* valPtr = Find(original, extractionSpec);

    if (valPtr == NULL)
    {
        return NULL;
    }

    const char* endPtr = NULL;

    switch (*valPtr)
    {
        case '{':

            endPtr = SkipObject(valPtr);
            if (endPtr != NULL)
            {
                *dataTypePtr = IO_DATA_TYPE_JSON;
                char object[HUB_MAX_STRING_BYTES];
                size_t objSize = (endPtr - valPtr);
                LE_ASSERT(objSize < sizeof(object));
                strncpy(object, valPtr, objSize);
                object[objSize] = '\0';
                return dataSample_CreateJson(dataSample_GetTimestamp(sampleRef), object);
            }
            break;

        case '[':

            endPtr = SkipArray(valPtr);
            if (endPtr != NULL)
            {
                *dataTypePtr = IO_DATA_TYPE_JSON;
                char object[HUB_MAX_STRING_BYTES];
                size_t objSize = (endPtr - valPtr);
                LE_ASSERT(objSize < sizeof(object));
                strncpy(object, valPtr, objSize);
                object[objSize] = '\0';
                return dataSample_CreateJson(dataSample_GetTimestamp(sampleRef), object);
            }
            break;

        case '"':

            endPtr = SkipString(valPtr);
            if (endPtr != NULL)
            {
                *dataTypePtr = IO_DATA_TYPE_STRING;

                // Move inside the quotes.
                valPtr++;
                endPtr--;

                char string[HUB_MAX_STRING_BYTES];
                size_t stringLen = (endPtr - valPtr);
                LE_ASSERT(stringLen < sizeof(string));
                strncpy(string, valPtr, stringLen);
                string[stringLen] = '\0';
                return dataSample_CreateString(dataSample_GetTimestamp(sampleRef), string);
            }
            break;

        case 't':

            if (strncmp(valPtr, "true", 4) == 0)
            {
                endPtr = valPtr + 4;
                *dataTypePtr = IO_DATA_TYPE_BOOLEAN;
                return dataSample_CreateBoolean(dataSample_GetTimestamp(sampleRef), true);
            }
            break;

        case 'f':

            if (strncmp(valPtr, "false", 5) == 0)
            {
                endPtr = valPtr + 5;
                *dataTypePtr = IO_DATA_TYPE_BOOLEAN;
                return dataSample_CreateBoolean(dataSample_GetTimestamp(sampleRef), false);
            }
            break;

        case 'n':

            if (strncmp(valPtr, "null", 4) == 0)
            {
                endPtr = valPtr + 4;
                *dataTypePtr = IO_DATA_TYPE_TRIGGER;
                return dataSample_CreateTrigger(dataSample_GetTimestamp(sampleRef));
            }
            break;

        default:
        {
            errno = 0;
            char* nonConstEndPtr;
            double number = strtod(valPtr, &nonConstEndPtr);
            endPtr = nonConstEndPtr;
            if ((endPtr != valPtr) && (errno == 0))
            {
                *dataTypePtr = IO_DATA_TYPE_NUMERIC;
                return dataSample_CreateNumeric(dataSample_GetTimestamp(sampleRef), number);
            }
            break;
        }
    }

    LE_ERROR("Invalid content in JSON string '%s' beginning at position %zu.",
             original,
             valPtr - original);

    return NULL;
}

