#pragma once

enum CMS_STATUS {
/* ERROR conditions */
    CMS_MISC_ERROR = -1,	/* A miscellaneous error occurred. */
    CMS_UPDATE_ERROR = -2,	/* An error occurred during an update. */
    CMS_INTERNAL_ACCESS_ERROR = -3,	/* An error occurred during an
					   internal access function. */
    CMS_NO_MASTER_ERROR = -4,	/* An error occurred because the master was
				   not started */
    CMS_CONFIG_ERROR = -5,	/* There was an error in the configuration */
    CMS_TIMED_OUT = -6,		/* operation timed out. */
    CMS_QUEUE_FULL = -7,	/* A write failed because queuing was enabled 
				   but there was no room to add to the queue. 
				 */
    CMS_CREATE_ERROR = -8,	/* Something could not be created.  */
    CMS_PERMISSIONS_ERROR = -9,	/* Problem with permissions */
    CMS_NO_SERVER_ERROR = -10,	/* The server has not been started or could
				   not be contacted. */
    CMS_RESOURCE_CONFLICT_ERROR = -11,	/* Two or more CMS buffers are trying 
					   to use the same resource. */
    CMS_NO_IMPLEMENTATION_ERROR = -12,	/* An operation was attempted which
					   has not yet been implemented for
					   the current platform or protocol. */
    CMS_INSUFFICIENT_SPACE_ERROR = -13,	/* The size of the buffer was
					   insufficient for the requested
					   operation. */
    CMS_LIBRARY_UNAVAILABLE_ERROR = -14,	/* A DLL or Shared Object
						   library needed for the
						   current protocol could not 
						   be found or initialized. */
    CMS_SERVER_SIDE_ERROR = -15,	/* The server reported an error. */
    CMS_NO_BLOCKING_SEM_ERROR = -16,	/* A blocking_read operartion was
					   tried but no semaphore for the
					   blocking was configured or
					   available. */

/* NON Error Conditions.*/
    CMS_STATUS_NOT_SET = 0,	/* The status variable has not been set yet. */
    CMS_READ_OLD = 1,		/* Read successful, but data is old. */
    CMS_READ_OK = 2,		/* Read successful so far. */
    CMS_WRITE_OK = 3,		/* Write successful so far. */
    CMS_WRITE_WAS_BLOCKED = 4,	/* Write if read did not succeed, because the 
				   buffer had not been read yet. */
    CMS_CLEAR_OK = 5,		/* A clear operation was successful.  */
    CMS_CLOSED = 6		/* The channel has been closed.  */
};